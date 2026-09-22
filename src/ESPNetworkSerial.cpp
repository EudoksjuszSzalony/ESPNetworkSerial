#include "ESPNetworkSerial.h"

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
    : _port(port), _server(port), _client(), _started(false) {}

void ESPNetworkSerialTCP::begin() {
  if (_started) {
    return;
  }

  _server.begin();
  _started = true;
}

void ESPNetworkSerialTCP::end() {
  if (_client) {
    _client.stop();
  }

  if (_started) {
    _server.stop();
    _started = false;
  }
}

void ESPNetworkSerialTCP::handle() {
  if (!_started) {
    return;
  }

  if (_client && !_client.connected()) {
    _client.stop();
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
  return _client && _client.connected();
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

  if (!_client || !_client.connected()) {
    return 0;
  }

  return _client.write(byte);
}

size_t ESPNetworkSerialTCP::write(const uint8_t *buffer, size_t size) {
  handle();

  if (!_client || !_client.connected() || buffer == nullptr || size == 0) {
    return 0;
  }

  return _client.write(buffer, size);
}

int ESPNetworkSerialTCP::available() {
  handle();

  if (!_client || !_client.connected()) {
    return 0;
  }

  return _client.available();
}

int ESPNetworkSerialTCP::read() {
  handle();

  if (!_client || !_client.connected()) {
    return -1;
  }

  return _client.read();
}

int ESPNetworkSerialTCP::peek() {
  handle();

  if (!_client || !_client.connected()) {
    return -1;
  }

  return _client.peek();
}

void ESPNetworkSerialTCP::flush() {
  if (_client && _client.connected()) {
    _client.flush();
  }
}
