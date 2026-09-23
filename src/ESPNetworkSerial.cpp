#include "ESPNetworkSerial.h"

#include <cstring>
#include <cstdio>

#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/gcm.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

ESPNetworkSerial ESPSerial;

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

size_t ESPNetworkSerialMux::read(uint8_t *buffer, size_t size) {
  if (buffer == nullptr || size == 0 || _streamCount == 0) {
    return 0;
  }

  for (size_t offset = 0; offset < _streamCount; ++offset) {
    const size_t index = (_nextReadIndex + offset) % _streamCount;
    Stream *stream = _streams[index];

    const int count = stream->available();
    if (count <= 0) {
      continue;
    }

    size_t request = static_cast<size_t>(count);
    if (request > size) {
      request = size;
    }

    size_t received = 0;
    if (stream == nullptr) {
      continue;
    }

    // Stream itself has no non-blocking bulk-read virtual. Use the optimized
    // ESPNetworkSerialTCP path when available; companion streams fall back to
    // byte reads while data remains immediately available.
    ESPNetworkSerialTCP *network = dynamic_cast<ESPNetworkSerialTCP *>(stream);
    if (network != nullptr) {
      received = network->read(buffer, request);
    } else {
      while (received < request && stream->available() > 0) {
        const int value = stream->read();
        if (value < 0) {
          break;
        }
        buffer[received++] = static_cast<uint8_t>(value);
      }
    }

    if (received > 0) {
      _nextReadIndex = (index + 1) % _streamCount;
      return received;
    }
  }

  return 0;
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


ESPNetworkSerial::ESPNetworkSerial(uint16_t port)
    : _mux(), _network(port) {
  _mux.addStream(_network);
}

void ESPNetworkSerial::begin() {
  _network.begin();
}

void ESPNetworkSerial::end() {
  _network.end();
}

void ESPNetworkSerial::handle() {
  _network.handle();
}

bool ESPNetworkSerial::addStream(Stream &stream) {
  if (&stream == &_network) {
    return true;
  }
  return _mux.addStream(stream);
}

bool ESPNetworkSerial::removeStream(Stream &stream) {
  if (&stream == &_network) {
    return false;
  }
  return _mux.removeStream(stream);
}

void ESPNetworkSerial::clearStreams() {
  _mux.clearStreams();
  _mux.addStream(_network);
}

size_t ESPNetworkSerial::streamCount() const {
  return _mux.streamCount();
}

bool ESPNetworkSerial::setAuthKey(const char *key) {
  return _network.setAuthKey(key);
}

void ESPNetworkSerial::clearAuthKey() {
  _network.clearAuthKey();
}

bool ESPNetworkSerial::authenticationEnabled() const {
  return _network.authenticationEnabled();
}

bool ESPNetworkSerial::waitForConnection() {
  return _network.waitForConnection();
}

bool ESPNetworkSerial::waitForConnection(uint32_t timeoutMs) {
  return _network.waitForConnection(timeoutMs);
}

bool ESPNetworkSerial::started() const {
  return _network.started();
}

bool ESPNetworkSerial::connected() {
  return _network.connected();
}

uint16_t ESPNetworkSerial::port() const {
  return _network.port();
}

IPAddress ESPNetworkSerial::remoteIP() {
  return _network.remoteIP();
}

ESPNetworkSerialTCP &ESPNetworkSerial::tcp() {
  return _network;
}

const ESPNetworkSerialTCP &ESPNetworkSerial::tcp() const {
  return _network;
}

size_t ESPNetworkSerial::write(uint8_t byte) {
  return _mux.write(byte);
}

size_t ESPNetworkSerial::write(const uint8_t *buffer, size_t size) {
  return _mux.write(buffer, size);
}

int ESPNetworkSerial::available() {
  return _mux.available();
}

int ESPNetworkSerial::read() {
  return _mux.read();
}

size_t ESPNetworkSerial::read(uint8_t *buffer, size_t size) {
  return _mux.read(buffer, size);
}

int ESPNetworkSerial::peek() {
  return _mux.peek();
}

void ESPNetworkSerial::flush() {
  _mux.flush();
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
      _serverNonceHex{},
      _secureMode(false),
      _txKey{},
      _rxKey{},
      _txNoncePrefix{},
      _rxNoncePrefix{},
      _txSequence(0),
      _rxSequence(0),
      _rxRecordHeader{},
      _rxRecordHeaderLength(0),
      _rxCipherLength(0),
      _rxCipher{},
      _rxCipherReceived(0),
      _rxTag{},
      _rxTagReceived(0),
      _rxPlain{},
      _rxPlainLength(0),
      _rxPlainOffset(0) {}

namespace {

constexpr char kESPNSecureMode[] = "aes256-gcm";

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

bool hmacSha256(const uint8_t *key, size_t keyLength, const uint8_t *data,
                size_t dataLength, uint8_t output[32]) {
  const mbedtls_md_info_t *info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return false;
  }

  return mbedtls_md_hmac(info, key, keyLength, data, dataLength, output) == 0;
}

bool hkdfExpandSingleBlock(const uint8_t prk[32], const char *infoText,
                           uint8_t *output, size_t outputLength) {
  if (infoText == nullptr || output == nullptr || outputLength == 0 ||
      outputLength > 32) {
    return false;
  }

  const size_t infoLength = std::strlen(infoText);
  uint8_t input[96];
  if (infoLength + 1 > sizeof(input)) {
    return false;
  }

  std::memcpy(input, infoText, infoLength);
  input[infoLength] = 0x01;

  uint8_t digest[32];
  if (!hmacSha256(prk, 32, input, infoLength + 1, digest)) {
    return false;
  }

  std::memcpy(output, digest, outputLength);
  std::memset(digest, 0, sizeof(digest));
  return true;
}

uint64_t readUint64BE(const uint8_t *input) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value = (value << 8) | input[i];
  }
  return value;
}

void writeUint64BE(uint8_t *output, uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    output[i] = static_cast<uint8_t>(value & 0xFF);
    value >>= 8;
  }
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

void ESPNetworkSerialTCP::resetRxRecordAssembly() {
  _rxRecordHeaderLength = 0;
  _rxCipherLength = 0;
  _rxCipherReceived = 0;
  _rxTagReceived = 0;
}

void ESPNetworkSerialTCP::resetSecureState() {
  _secureMode = false;
  std::memset(_txKey, 0, sizeof(_txKey));
  std::memset(_rxKey, 0, sizeof(_rxKey));
  std::memset(_txNoncePrefix, 0, sizeof(_txNoncePrefix));
  std::memset(_rxNoncePrefix, 0, sizeof(_rxNoncePrefix));
  _txSequence = 0;
  _rxSequence = 0;
  resetRxRecordAssembly();
  _rxPlainLength = 0;
  _rxPlainOffset = 0;
}

void ESPNetworkSerialTCP::resetProtocolState() {
  _protocolReady = false;
  _handshakeState = HandshakeState::WaitingHello;
  _handshakeStartedAt = 0;
  resetHandshakeLine();
  std::memset(_clientNonceHex, 0, sizeof(_clientNonceHex));
  std::memset(_serverNonceHex, 0, sizeof(_serverNonceHex));
  resetSecureState();
}

void ESPNetworkSerialTCP::closeProtocolClient() {
  if (_client) {
    _client.stop();
  }
  resetProtocolState();
}

bool ESPNetworkSerialTCP::computeHmac(const char *role, const char *mode,
                                      uint8_t output[32]) const {
  if (!authenticationEnabled() || role == nullptr || mode == nullptr ||
      output == nullptr) {
    return false;
  }

  char message[128];
  const int messageLength =
      std::snprintf(message, sizeof(message), "ESPNS/1 %s %s %s %s", role,
                    _clientNonceHex, _serverNonceHex, mode);
  if (messageLength <= 0 ||
      static_cast<size_t>(messageLength) >= sizeof(message)) {
    return false;
  }

  return hmacSha256(
      reinterpret_cast<const uint8_t *>(_authKey), _authKeyLength,
      reinterpret_cast<const uint8_t *>(message),
      static_cast<size_t>(messageLength), output);
}

bool ESPNetworkSerialTCP::deriveSessionKeys() {
  if (!authenticationEnabled()) {
    return false;
  }

  uint8_t clientNonce[ESPNETWORKSERIAL_AUTH_NONCE_SIZE];
  uint8_t serverNonce[ESPNETWORKSERIAL_AUTH_NONCE_SIZE];
  if (!hexToBytes(_clientNonceHex, sizeof(clientNonce), clientNonce) ||
      !hexToBytes(_serverNonceHex, sizeof(serverNonce), serverNonce)) {
    return false;
  }

  uint8_t salt[ESPNETWORKSERIAL_AUTH_NONCE_SIZE * 2];
  std::memcpy(salt, clientNonce, sizeof(clientNonce));
  std::memcpy(salt + sizeof(clientNonce), serverNonce, sizeof(serverNonce));

  uint8_t prk[32];
  if (!hmacSha256(
          salt, sizeof(salt),
          reinterpret_cast<const uint8_t *>(_authKey), _authKeyLength, prk)) {
    return false;
  }

  const bool ok =
      hkdfExpandSingleBlock(prk, "ESPNS/1 aes256-gcm device-to-host key",
                            _txKey, sizeof(_txKey)) &&
      hkdfExpandSingleBlock(prk, "ESPNS/1 aes256-gcm host-to-device key",
                            _rxKey, sizeof(_rxKey)) &&
      hkdfExpandSingleBlock(
          prk, "ESPNS/1 aes256-gcm device-to-host nonce-prefix",
          _txNoncePrefix, sizeof(_txNoncePrefix)) &&
      hkdfExpandSingleBlock(
          prk, "ESPNS/1 aes256-gcm host-to-device nonce-prefix",
          _rxNoncePrefix, sizeof(_rxNoncePrefix));

  std::memset(prk, 0, sizeof(prk));
  std::memset(clientNonce, 0, sizeof(clientNonce));
  std::memset(serverNonce, 0, sizeof(serverNonce));
  std::memset(salt, 0, sizeof(salt));

  _txSequence = 0;
  _rxSequence = 0;
  resetRxRecordAssembly();
  _rxPlainLength = 0;
  _rxPlainOffset = 0;
  return ok;
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
  if (!computeHmac("SERVER", kESPNSecureMode, proof)) {
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
  _client.print(" mode=");
  _client.print(kESPNSecureMode);
  _client.print("\n");

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
  if (!computeHmac("CLIENT", kESPNSecureMode, expectedProof)) {
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

    if (!deriveSessionKeys()) {
      _client.print("ESPNS/1 ERR secure_internal\n");
      closeProtocolClient();
      return;
    }

    _client.print("ESPNS/1 OK auth=hmac-sha256 mode=");
    _client.print(kESPNSecureMode);
    _client.print("\n");

    _secureMode = true;
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

void ESPNetworkSerialTCP::makeSecureNonce(
    const uint8_t prefix[ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE],
    uint64_t sequence, uint8_t nonce[12]) const {
  std::memcpy(nonce, prefix, ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE);
  writeUint64BE(nonce + ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE, sequence);
}

bool ESPNetworkSerialTCP::writeClientAll(const uint8_t *buffer, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const size_t written = _client.write(buffer + offset, size - offset);
    if (written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool ESPNetworkSerialTCP::writeSecureRecord(const uint8_t *buffer, size_t size) {
  if (!_secureMode || buffer == nullptr || size == 0 ||
      size > ESPNETWORKSERIAL_SECURE_MAX_RECORD ||
      _txSequence == UINT64_MAX) {
    return false;
  }

  uint8_t header[ESPNETWORKSERIAL_SECURE_HEADER_SIZE];
  header[0] = static_cast<uint8_t>((size >> 8) & 0xFF);
  header[1] = static_cast<uint8_t>(size & 0xFF);
  writeUint64BE(header + 2, _txSequence);

  uint8_t nonce[12];
  makeSecureNonce(_txNoncePrefix, _txSequence, nonce);

  uint8_t ciphertext[ESPNETWORKSERIAL_SECURE_MAX_RECORD];
  uint8_t tag[ESPNETWORKSERIAL_SECURE_TAG_SIZE];

  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);

  int result = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, _txKey,
      ESPNETWORKSERIAL_SECURE_KEY_SIZE * 8);
  if (result == 0) {
    result = mbedtls_gcm_crypt_and_tag(
        &context, MBEDTLS_GCM_ENCRYPT, size, nonce, sizeof(nonce), header,
        sizeof(header), buffer, ciphertext, sizeof(tag), tag);
  }

  mbedtls_gcm_free(&context);

  if (result != 0 ||
      !writeClientAll(header, sizeof(header)) ||
      !writeClientAll(ciphertext, size) ||
      !writeClientAll(tag, sizeof(tag))) {
    return false;
  }

  ++_txSequence;
  return true;
}

bool ESPNetworkSerialTCP::decryptSecureRecord() {
  if (_rxCipherLength == 0 ||
      _rxCipherLength > ESPNETWORKSERIAL_SECURE_MAX_RECORD ||
      _rxCipherReceived != _rxCipherLength ||
      _rxTagReceived != ESPNETWORKSERIAL_SECURE_TAG_SIZE) {
    return false;
  }

  const uint64_t sequence = readUint64BE(_rxRecordHeader + 2);
  if (sequence != _rxSequence || _rxSequence == UINT64_MAX) {
    return false;
  }

  uint8_t nonce[12];
  makeSecureNonce(_rxNoncePrefix, sequence, nonce);

  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);

  int result = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, _rxKey,
      ESPNETWORKSERIAL_SECURE_KEY_SIZE * 8);
  if (result == 0) {
    result = mbedtls_gcm_auth_decrypt(
        &context, _rxCipherLength, nonce, sizeof(nonce), _rxRecordHeader,
        sizeof(_rxRecordHeader), _rxTag, sizeof(_rxTag), _rxCipher, _rxPlain);
  }

  mbedtls_gcm_free(&context);

  if (result != 0) {
    return false;
  }

  _rxPlainLength = _rxCipherLength;
  _rxPlainOffset = 0;
  ++_rxSequence;
  return true;
}

void ESPNetworkSerialTCP::handleSecureRx() {
  if (!_secureMode || !_protocolReady || !_client || !_client.connected() ||
      _rxPlainOffset < _rxPlainLength) {
    return;
  }

  while (_client.available() > 0) {
    if (_rxRecordHeaderLength < ESPNETWORKSERIAL_SECURE_HEADER_SIZE) {
      const size_t remaining =
          ESPNETWORKSERIAL_SECURE_HEADER_SIZE - _rxRecordHeaderLength;
      const size_t availableBytes = static_cast<size_t>(_client.available());
      const size_t request =
          availableBytes < remaining ? availableBytes : remaining;

      const int count =
          _client.read(_rxRecordHeader + _rxRecordHeaderLength, request);
      if (count <= 0) {
        return;
      }

      _rxRecordHeaderLength += static_cast<size_t>(count);
      if (_rxRecordHeaderLength < ESPNETWORKSERIAL_SECURE_HEADER_SIZE) {
        return;
      }

      _rxCipherLength =
          static_cast<uint16_t>((static_cast<uint16_t>(_rxRecordHeader[0]) << 8) |
                                _rxRecordHeader[1]);

      const uint64_t sequence = readUint64BE(_rxRecordHeader + 2);
      if (_rxCipherLength == 0 ||
          _rxCipherLength > ESPNETWORKSERIAL_SECURE_MAX_RECORD ||
          sequence != _rxSequence) {
        closeProtocolClient();
        return;
      }
    }

    if (_rxCipherReceived < _rxCipherLength && _client.available() > 0) {
      const size_t remaining = _rxCipherLength - _rxCipherReceived;
      const size_t availableBytes = static_cast<size_t>(_client.available());
      const size_t request =
          availableBytes < remaining ? availableBytes : remaining;

      const int count = _client.read(_rxCipher + _rxCipherReceived, request);
      if (count <= 0) {
        return;
      }
      _rxCipherReceived += static_cast<size_t>(count);

      if (_rxCipherReceived < _rxCipherLength) {
        return;
      }
    }

    if (_rxTagReceived < ESPNETWORKSERIAL_SECURE_TAG_SIZE &&
        _client.available() > 0) {
      const size_t remaining =
          ESPNETWORKSERIAL_SECURE_TAG_SIZE - _rxTagReceived;
      const size_t availableBytes = static_cast<size_t>(_client.available());
      const size_t request =
          availableBytes < remaining ? availableBytes : remaining;

      const int count = _client.read(_rxTag + _rxTagReceived, request);
      if (count <= 0) {
        return;
      }
      _rxTagReceived += static_cast<size_t>(count);

      if (_rxTagReceived < ESPNETWORKSERIAL_SECURE_TAG_SIZE) {
        return;
      }
    }

    if (_rxRecordHeaderLength == ESPNETWORKSERIAL_SECURE_HEADER_SIZE &&
        _rxCipherReceived == _rxCipherLength &&
        _rxTagReceived == ESPNETWORKSERIAL_SECURE_TAG_SIZE) {
      if (!decryptSecureRecord()) {
        closeProtocolClient();
        return;
      }

      resetRxRecordAssembly();
      return;
    }

    return;
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

  if (_client && _client.connected() && _protocolReady && _secureMode) {
    handleSecureRx();
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
  return write(&byte, 1);
}

size_t ESPNetworkSerialTCP::write(const uint8_t *buffer, size_t size) {
  handle();

  if (!_client || !_client.connected() || !_protocolReady ||
      buffer == nullptr || size == 0) {
    return 0;
  }

  if (!_secureMode) {
    return _client.write(buffer, size);
  }

  size_t total = 0;
  while (total < size) {
    size_t chunk = size - total;
    if (chunk > ESPNETWORKSERIAL_SECURE_MAX_RECORD) {
      chunk = ESPNETWORKSERIAL_SECURE_MAX_RECORD;
    }

    if (!writeSecureRecord(buffer + total, chunk)) {
      closeProtocolClient();
      break;
    }

    total += chunk;
  }

  return total;
}

int ESPNetworkSerialTCP::available() {
  if (_secureMode && _protocolReady && _client && _client.connected() &&
      _rxPlainOffset < _rxPlainLength) {
    return static_cast<int>(_rxPlainLength - _rxPlainOffset);
  }

  handle();

  if (!_client || !_client.connected() || !_protocolReady) {
    return 0;
  }

  if (!_secureMode) {
    return _client.available();
  }

  handleSecureRx();
  return static_cast<int>(_rxPlainLength - _rxPlainOffset);
}

int ESPNetworkSerialTCP::read() {
  if (!(_secureMode && _protocolReady && _client && _client.connected() &&
        _rxPlainOffset < _rxPlainLength)) {
    handle();
  }

  if (!_client || !_client.connected() || !_protocolReady) {
    return -1;
  }

  if (!_secureMode) {
    return _client.read();
  }

  if (_rxPlainOffset >= _rxPlainLength) {
    handleSecureRx();
  }
  if (_rxPlainOffset >= _rxPlainLength) {
    return -1;
  }

  const int value = _rxPlain[_rxPlainOffset++];
  if (_rxPlainOffset >= _rxPlainLength) {
    _rxPlainOffset = 0;
    _rxPlainLength = 0;
  }
  return value;
}

size_t ESPNetworkSerialTCP::read(uint8_t *buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return 0;
  }

  if (!(_secureMode && _protocolReady && _client && _client.connected() &&
        _rxPlainOffset < _rxPlainLength)) {
    handle();
  }

  if (!_client || !_client.connected() || !_protocolReady) {
    return 0;
  }

  if (!_secureMode) {
    const int availableBytes = _client.available();
    if (availableBytes <= 0) {
      return 0;
    }

    size_t request = static_cast<size_t>(availableBytes);
    if (request > size) {
      request = size;
    }

    const int count = _client.read(buffer, request);
    return count > 0 ? static_cast<size_t>(count) : 0;
  }

  if (_rxPlainOffset >= _rxPlainLength) {
    handleSecureRx();
  }
  if (_rxPlainOffset >= _rxPlainLength) {
    return 0;
  }

  size_t count = _rxPlainLength - _rxPlainOffset;
  if (count > size) {
    count = size;
  }

  std::memcpy(buffer, _rxPlain + _rxPlainOffset, count);
  _rxPlainOffset += count;

  if (_rxPlainOffset >= _rxPlainLength) {
    _rxPlainOffset = 0;
    _rxPlainLength = 0;
  }

  return count;
}

int ESPNetworkSerialTCP::peek() {
  if (!(_secureMode && _protocolReady && _client && _client.connected() &&
        _rxPlainOffset < _rxPlainLength)) {
    handle();
  }

  if (!_client || !_client.connected() || !_protocolReady) {
    return -1;
  }

  if (!_secureMode) {
    return _client.peek();
  }

  if (_rxPlainOffset >= _rxPlainLength) {
    handleSecureRx();
  }
  if (_rxPlainOffset >= _rxPlainLength) {
    return -1;
  }

  return _rxPlain[_rxPlainOffset];
}

void ESPNetworkSerialTCP::flush() {
  if (_client && _client.connected() && _protocolReady) {
    _client.flush();
  }
}
