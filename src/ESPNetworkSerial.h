#pragma once

#include <Arduino.h>
#include <WiFi.h>

#ifndef ESPNETWORKSERIAL_MAX_STREAMS
#define ESPNETWORKSERIAL_MAX_STREAMS 4
#endif

#ifndef ESPNETWORKSERIAL_DEFAULT_PORT
#define ESPNETWORKSERIAL_DEFAULT_PORT 3233
#endif

class ESPNetworkSerialMux : public Stream {
public:
  ESPNetworkSerialMux();

  bool addStream(Stream &stream);
  bool removeStream(Stream &stream);
  void clearStreams();
  size_t streamCount() const;

  using Print::write;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

private:
  Stream *_streams[ESPNETWORKSERIAL_MAX_STREAMS];
  size_t _streamCount;
  size_t _nextReadIndex;
};

class ESPNetworkSerialTCP : public Stream {
public:
  explicit ESPNetworkSerialTCP(uint16_t port = ESPNETWORKSERIAL_DEFAULT_PORT);

  void begin();
  void end();
  void handle();

  // Optional boot-time wait helpers:
  //   no wait: do not call waitForConnection()
  //   required: waitForConnection()
  //   timeout: waitForConnection(12000)
  bool waitForConnection();
  bool waitForConnection(uint32_t timeoutMs);

  bool started() const;
  bool connected();
  uint16_t port() const;
  IPAddress remoteIP();

  using Print::write;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

private:
  uint16_t _port;
  WiFiServer _server;
  WiFiClient _client;
  bool _started;
};

extern ESPNetworkSerialMux ESPSerial;
