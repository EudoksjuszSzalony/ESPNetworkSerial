#include "ESPNetworkSerial.h"

#include <cstring>
#include <cstdio>

#include <esp_random.h>
#include <mbedtls/md.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

ESPNetworkSerialMux ESPSerial;

ESPNetworkSerialMux::ESPNetworkSerialMux()
    : _streams{}, _streamCount(0), _nextReadIndex(0) {}

bool ESPNetworkSerialMux::addStream(Stream &stream) {
  for (size_t i = 0; i < _streamCount; ++i) {
    if (_streams[i] == &stream) {
      return true;
    }
  }

  if (_streamCount >= ESPNETWORKSERIAL_MAX_STREAMS) {
    return false;
  }

  _streams[_streamCount++] = &stream;
  return true;
}

bool ESPNetworkSerialMux::removeStream(Stream &stream) {
  for (size_t i = 0; i < _streamCount; ++i) {
    if (_streams[i] != &stream) {
      continue;
    }

    for (size_t j = i + 1; j < _streamCount; ++j) {
      _streams[j - 1] = _streams[j];
    }

    _streams[_streamCount - 1] = nullptr;
    --_streamCount;

    if (_streamCount == 0 || _nextReadIndex >= _streamCount) {
      _nextReadIndex = 0;
    }

    return true;
  }

  return false;
}

void ESPNetworkSerialMux::clearStreams() {
  for (size_t i = 0; i < ESPNETWORKSERIAL_MAX_STREAMS; ++i) {
    _streams[i] = nullptr;
  }

  _streamCount = 0;
  _nextReadIndex = 0;
}

size_t ESPNetworkSerialMux::streamCount() const {
  return _streamCount;
}

size_t ESPNetworkSerialMux::write(uint8_t byte) {
  size_t bestResult = 0;

  for (size_t i = 0; i < _streamCount; ++i) {
    const size_t written = _streams[i]->write(byte);
    if (written > bestResult) {
      bestResult = written;
    }
  }

  return bestResult;
}

size_t ESPNetworkSerialMux::write(const uint8_t *buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return 0;
  }

  size_t bestResult = 0;

  for (size_t i = 0; i < _streamCount; ++i) {
    const size_t written = _streams[i]->write(buffer, size);
    if (written > bestResult) {
      bestResult = written;
    }
  }

  return bestResult;
}

int ESPNetworkSerialMux::available() {
  int total = 0;

  for (size_t i = 0; i < _streamCount; ++i) {
    const int count = _streams[i]->available();
    if (count > 0) {
      total += count;
    }
  }

  return total;
}

int ESPNetworkSerialMux::read() {
  if (_streamCount == 0) {
    return -1;
  }

  for (size_t offset = 0; offset < _streamCount; ++offset) {
    const size_t index = (_nextReadIndex + offset) % _streamCount;
    Stream *stream = _streams[index];

    if (stream->available() <= 0) {
      continue;
    }

    const int value = stream->read();
    _nextReadIndex = (index + 1) % _streamCount;
    return value;
  }

  return -1;
}

int ESPNetworkSerialMux::peek() {
  if (_streamCount == 0) {
    return -1;
  }

  for (size_t offset = 0; offset < _streamCount; ++offset) {
    const size_t index = (_nextReadIndex + offset) % _streamCount;
    Stream *stream = _streams[index];

    if (stream->available() > 0) {
      return stream->peek();
    }
  }

  return -1;
}

void ESPNetworkSerialMux::flush() {
  for (size_t i = 0; i < _streamCount; ++i) {
    _streams[i]->flush();
  }
}

ESPNetworkSerialTCP::ESPNetworkSerialTCP(uint16_t port)
    : _port(port),
      _server(port),
      _client(),
      _started(false),
      _protocolReady(false),
      _handshakeState(HandshakeState::WaitingHello),
      _handshakeStartedAt(0),
      _handshakeLength(0),
      _handshakeBuffer{},
      _authKey{},
      _authKeyLength(0),
      _clientNonceHex{},
      _serverNonceHex{} {}

namespace {

char hexDigit(uint8_t value) {
  value &= 0x0F;
  return value < 10 ? static_cast<char>('0' + value)
                    : static_cast<char>('a' + (value - 10));
}

int hexValue(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

void bytesToHex(const uint8_t *input, size_t length, char *output) {
  for (size_t i = 0; i < length; ++i) {
    output[i * 2] = hexDigit(input[i] >> 4);
    output[(i * 2) + 1] = hexDigit(input[i]);
  }
  output[length * 2] = '\0';
}

bool hexToBytes(const char *input, size_t expectedBytes, uint8_t *output) {
  if (input == nullptr || std::strlen(input) != expectedBytes * 2) {
    return false;
  }

  for (size_t i = 0; i < expectedBytes; ++i) {
    const int high = hexValue(input[i * 2]);
    const int low = hexValue(input[(i * 2) + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    output[i] = static_cast<uint8_t>((high << 4) | low);
  }

  return true;
}

bool constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t length) {
  uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) {
    difference |= left[i] ^ right[i];
  }
  return difference == 0;
}

bool getControlField(const char *line, const char *fieldName, char *output,
                     size_t outputSize) {
  if (line == nullptr || fieldName == nullptr || output == nullptr ||
      outputSize == 0) {
    return false;
  }

  const size_t fieldNameLength = std::strlen(fieldName);
  const char *cursor = line;

  while (*cursor != '\0') {
    while (*cursor == ' ') {
      ++cursor;
    }

    const char *tokenStart = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
      ++cursor;
    }

    const size_t tokenLength = static_cast<size_t>(cursor - tokenStart);
    if (tokenLength > fieldNameLength &&
        std::strncmp(tokenStart, fieldName, fieldNameLength) == 0) {
      const size_t valueLength = tokenLength - fieldNameLength;
      if (valueLength + 1 > outputSize) {
        return false;
      }

      std::memcpy(output, tokenStart + fieldNameLength, valueLength);
      output[valueLength] = '\0';
      return true;
    }
  }

  return false;
}

}  // namespace

void ESPNetworkSerialTCP::begin() {
  if (_started) {
    return;
  }

  _server.begin();
  _started = true;
}

void ESPNetworkSerialTCP::end() {
  closeProtocolClient();

  if (_started) {
    _server.stop();
    _started = false;
  }
}

bool ESPNetworkSerialTCP::setAuthKey(const char *key) {
  if (key == nullptr) {
    return false;
  }

  const size_t length = std::strlen(key);
  if (length < ESPNETWORKSERIAL_AUTH_KEY_MIN_LENGTH ||
      length > ESPNETWORKSERIAL_AUTH_KEY_MAX_LENGTH) {
    return false;
  }

  std::memset(_authKey, 0, sizeof(_authKey));
  std::memcpy(_authKey, key, length);
  _authKeyLength = length;

  if (_client) {
    closeProtocolClient();
  }

  return true;
}

void ESPNetworkSerialTCP::clearAuthKey() {
  std::memset(_authKey, 0, sizeof(_authKey));
  _authKeyLength = 0;

  if (_client) {
    closeProtocolClient();
  }
}

bool ESPNetworkSerialTCP::authenticationEnabled() const {
  return _authKeyLength > 0;
}

void ESPNetworkSerialTCP::resetHandshakeLine() {
  _handshakeLength = 0;
  _handshakeBuffer[0] = '\0';
}

void ESPNetworkSerialTCP::resetProtocolState() {
  _protocolReady = false;
  _handshakeState = HandshakeState::WaitingHello;
  _handshakeStartedAt = 0;
  resetHandshakeLine();
  std::memset(_clientNonceHex, 0, sizeof(_clientNonceHex));
  std::memset(_serverNonceHex, 0, sizeof(_serverNonceHex));
}

void ESPNetworkSerialTCP::closeProtocolClient() {
  if (_client) {
    _client.stop();
  }
  resetProtocolState();
}

bool ESPNetworkSerialTCP::computeHmac(const char *role,
                                      uint8_t output[32]) const {
  if (!authenticationEnabled() || role == nullptr || output == nullptr) {
    return false;
  }

  char message[96];
  const int messageLength =
      std::snprintf(message, sizeof(message), "ESPNS/1 %s %s %s", role,
                    _clientNonceHex, _serverNonceHex);
  if (messageLength <= 0 ||
      static_cast<size_t>(messageLength) >= sizeof(message)) {
    return false;
  }

  const mbedtls_md_info_t *info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return false;
  }

  return mbedtls_md_hmac(
             info, reinterpret_cast<const unsigned char *>(_authKey),
             _authKeyLength,
             reinterpret_cast<const unsigned char *>(message),
             static_cast<size_t>(messageLength), output) == 0;
}

void ESPNetworkSerialTCP::beginAuthChallenge(const char *clientNonceHex) {
  if (clientNonceHex == nullptr ||
      std::strlen(clientNonceHex) != ESPNETWORKSERIAL_AUTH_NONCE_SIZE * 2) {
    _client.print("ESPNS/1 ERR invalid_nonce\n");
    closeProtocolClient();
    return;
  }

  uint8_t decodedClientNonce[ESPNETWORKSERIAL_AUTH_NONCE_SIZE];
  if (!hexToBytes(clientNonceHex, ESPNETWORKSERIAL_AUTH_NONCE_SIZE,
                  decodedClientNonce)) {
    _client.print("ESPNS/1 ERR invalid_nonce\n");
    closeProtocolClient();
    return;
  }

  std::strncpy(_clientNonceHex, clientNonceHex, sizeof(_clientNonceHex) - 1);
  _clientNonceHex[sizeof(_clientNonceHex) - 1] = '\0';

  uint8_t serverNonce[ESPNETWORKSERIAL_AUTH_NONCE_SIZE];
  esp_fill_random(serverNonce, sizeof(serverNonce));
  bytesToHex(serverNonce, sizeof(serverNonce), _serverNonceHex);

  uint8_t proof[32];
  if (!computeHmac("SERVER", proof)) {
    _client.print("ESPNS/1 ERR auth_internal\n");
    closeProtocolClient();
    return;
  }

  char proofHex[65];
  bytesToHex(proof, sizeof(proof), proofHex);

  _client.print("ESPNS/1 CHALLENGE auth=hmac-sha256 nonce=");
  _client.print(_serverNonceHex);
  _client.print(" proof=");
  _client.print(proofHex);
  _client.print(" mode=raw\n");

  _handshakeState = HandshakeState::WaitingAuth;
  _handshakeStartedAt = millis();
  resetHandshakeLine();
}

bool ESPNetworkSerialTCP::verifyClientProof(const char *proofHex) {
  uint8_t suppliedProof[32];
  if (!hexToBytes(proofHex, sizeof(suppliedProof), suppliedProof)) {
    return false;
  }

  uint8_t expectedProof[32];
  if (!computeHmac("CLIENT", expectedProof)) {
    return false;
  }

  return constantTimeEqual(suppliedProof, expectedProof, sizeof(expectedProof));
}

void ESPNetworkSerialTCP::processHandshakeLine() {
  _handshakeBuffer[_handshakeLength] = '\0';

  if (_handshakeState == HandshakeState::WaitingHello) {
    const char *helloPrefix = "ESPNS/1 HELLO";
    const size_t helloPrefixLength = std::strlen(helloPrefix);
    if (std::strncmp(_handshakeBuffer, helloPrefix, helloPrefixLength) != 0 ||
        (_handshakeBuffer[helloPrefixLength] != '\0' &&
         _handshakeBuffer[helloPrefixLength] != ' ')) {
      _client.print("ESPNS/1 ERR bad_hello\n");
      closeProtocolClient();
      return;
    }

    if (!authenticationEnabled()) {
      _client.print("ESPNS/1 OK auth=none mode=raw\n");
      _protocolReady = true;
      _handshakeState = HandshakeState::Ready;
      return;
    }

    char clientNonce[(ESPNETWORKSERIAL_AUTH_NONCE_SIZE * 2) + 1];
    if (!getControlField(_handshakeBuffer, "nonce=", clientNonce,
                         sizeof(clientNonce))) {
      _client.print("ESPNS/1 ERR nonce_required\n");
      closeProtocolClient();
      return;
    }

    beginAuthChallenge(clientNonce);
    return;
  }

  if (_handshakeState == HandshakeState::WaitingAuth) {
    const char *authPrefix = "ESPNS/1 AUTH";
    const size_t authPrefixLength = std::strlen(authPrefix);
    if (std::strncmp(_handshakeBuffer, authPrefix, authPrefixLength) != 0 ||
        (_handshakeBuffer[authPrefixLength] != '\0' &&
         _handshakeBuffer[authPrefixLength] != ' ')) {
      _client.print("ESPNS/1 ERR bad_auth\n");
      closeProtocolClient();
      return;
    }

    char proofHex[65];
    if (!getControlField(_handshakeBuffer, "proof=", proofHex,
                         sizeof(proofHex)) ||
        !verifyClientProof(proofHex)) {
      _client.print("ESPNS/1 ERR auth_failed\n");
      closeProtocolClient();
      return;
    }

    _client.print("ESPNS/1 OK auth=hmac-sha256 mode=raw\n");
    _protocolReady = true;
    _handshakeState = HandshakeState::Ready;
    return;
  }
}

void ESPNetworkSerialTCP::handleHandshake() {
  if (!_client || !_client.connected() || _protocolReady) {
    return;
  }

  while (_client.available() > 0) {
    const int value = _client.read();
    if (value < 0) {
      break;
    }

    if (value == '\r') {
      continue;
    }

    if (value == '\n') {
      processHandshakeLine();
      if (!_client || !_client.connected() || _protocolReady) {
        return;
      }

      if (_handshakeState != HandshakeState::WaitingAuth) {
        resetHandshakeLine();
      }
      continue;
    }

    if (_handshakeLength + 1 >= sizeof(_handshakeBuffer)) {
      _client.print("ESPNS/1 ERR line_too_long\n");
      closeProtocolClient();
      return;
    }

    _handshakeBuffer[_handshakeLength++] = static_cast<char>(value);
  }

  if (_handshakeStartedAt != 0 &&
      static_cast<uint32_t>(millis() - _handshakeStartedAt) >=
          ESPNETWORKSERIAL_HANDSHAKE_TIMEOUT_MS) {
    _client.print("ESPNS/1 ERR timeout\n");
    closeProtocolClient();
  }
}

void ESPNetworkSerialTCP::handle() {
  if (!_started) {
    return;
  }

  if (_client && !_client.connected()) {
    closeProtocolClient();
  }

  if (_client && _client.connected() && !_protocolReady) {
    handleHandshake();
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  WiFiClient candidate = _server.accept();
#else
  WiFiClient candidate = _server.available();
#endif

  if (!candidate) {
    return;
  }

  if (_client && _client.connected()) {
    candidate.stop();
    return;
  }

  _client = candidate;
  _client.setNoDelay(true);
  resetProtocolState();
  _handshakeStartedAt = millis();
  handleHandshake();
}

bool ESPNetworkSerialTCP::waitForConnection() {
  if (!_started) {
    begin();
  }

  while (!connected()) {
    delay(1);
  }

  return true;
}

bool ESPNetworkSerialTCP::waitForConnection(uint32_t timeoutMs) {
  if (!_started) {
    begin();
  }

  if (timeoutMs == 0) {
    return connected();
  }

  const uint32_t startedAt = millis();

  while (!connected()) {
    if (static_cast<uint32_t>(millis() - startedAt) >= timeoutMs) {
      return false;
    }

    delay(1);
  }

  return true;
}

bool ESPNetworkSerialTCP::started() const {
  return _started;
}

bool ESPNetworkSerialTCP::connected() {
  handle();
  return _client && _client.connected() && _protocolReady;
}

uint16_t ESPNetworkSerialTCP::port() const {
  return _port;
}

IPAddress ESPNetworkSerialTCP::remoteIP() {
  if (!_client) {
    return IPAddress();
  }

  return _client.remoteIP();
}

size_t ESPNetworkSerialTCP::write(uint8_t byte) {
  handle();

  if (!_client || !_client.connected() || !_protocolReady) {
    return 0;
  }

  return _client.write(byte);
}

size_t ESPNetworkSerialTCP::write(const uint8_t *buffer, size_t size) {
  handle();

  if (!_client || !_client.connected() || !_protocolReady ||
      buffer == nullptr || size == 0) {
    return 0;
  }

  return _client.write(buffer, size);
}

int ESPNetworkSerialTCP::available() {
  handle();

  if (!_client || !_client.connected() || !_protocolReady) {
    return 0;
  }

  return _client.available();
}

int ESPNetworkSerialTCP::read() {
  handle();

  if (!_client || !_client.connected() || !_protocolReady) {
    return -1;
  }

  return _client.read();
}

int ESPNetworkSerialTCP::peek() {
  handle();

  if (!_client || !_client.connected() || !_protocolReady) {
    return -1;
  }

  return _client.peek();
}

void ESPNetworkSerialTCP::flush() {
  if (_client && _client.connected() && _protocolReady) {
    _client.flush();
  }
}
