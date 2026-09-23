#pragma once

#include <Arduino.h>
#include <WiFi.h>

#ifndef ESPNETWORKSERIAL_MAX_STREAMS
#define ESPNETWORKSERIAL_MAX_STREAMS 4
#endif

#ifndef ESPNETWORKSERIAL_DEFAULT_PORT
#define ESPNETWORKSERIAL_DEFAULT_PORT 3233
#endif

#ifndef ESPNETWORKSERIAL_PROTOCOL_VERSION
#define ESPNETWORKSERIAL_PROTOCOL_VERSION 1
#endif

#ifndef ESPNETWORKSERIAL_HANDSHAKE_TIMEOUT_MS
#define ESPNETWORKSERIAL_HANDSHAKE_TIMEOUT_MS 2500
#endif

#ifndef ESPNETWORKSERIAL_HANDSHAKE_MAX_LENGTH
#define ESPNETWORKSERIAL_HANDSHAKE_MAX_LENGTH 256
#endif

#ifndef ESPNETWORKSERIAL_AUTH_KEY_MIN_LENGTH
#define ESPNETWORKSERIAL_AUTH_KEY_MIN_LENGTH 16
#endif

#ifndef ESPNETWORKSERIAL_AUTH_KEY_MAX_LENGTH
#define ESPNETWORKSERIAL_AUTH_KEY_MAX_LENGTH 128
#endif

#ifndef ESPNETWORKSERIAL_AUTH_NONCE_SIZE
#define ESPNETWORKSERIAL_AUTH_NONCE_SIZE 16
#endif

#ifndef ESPNETWORKSERIAL_SECURE_MAX_RECORD
#define ESPNETWORKSERIAL_SECURE_MAX_RECORD 1024
#endif

#ifndef ESPNETWORKSERIAL_SECURE_TAG_SIZE
#define ESPNETWORKSERIAL_SECURE_TAG_SIZE 16
#endif

#ifndef ESPNETWORKSERIAL_SECURE_HEADER_SIZE
#define ESPNETWORKSERIAL_SECURE_HEADER_SIZE 10
#endif

#ifndef ESPNETWORKSERIAL_SECURE_KEY_SIZE
#define ESPNETWORKSERIAL_SECURE_KEY_SIZE 32
#endif

#ifndef ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE
#define ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE 4
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
  size_t read(uint8_t *buffer, size_t size);
  size_t read(uint8_t *buffer, size_t size, Stream *bulkStream);
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

  // Optional ESPNS mutual authentication. The key is independent from any
  // ArduinoOTA password. A configured key requires HMAC-SHA256 authentication.
  bool setAuthKey(const char *key);
  void clearAuthKey();
  bool authenticationEnabled() const;

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
  size_t read(uint8_t *buffer, size_t size);
  int peek() override;
  void flush() override;

private:
  enum class HandshakeState : uint8_t {
    WaitingHello,
    WaitingAuth,
    Ready,
  };

  void resetProtocolState();
  void resetHandshakeLine();
  void handleHandshake();
  void processHandshakeLine();
  void closeProtocolClient();
  void beginAuthChallenge(const char *clientNonceHex);
  bool verifyClientProof(const char *proofHex);
  bool computeHmac(const char *role, const char *mode, uint8_t output[32]) const;
  bool deriveSessionKeys();

  void resetSecureState();
  void resetRxRecordAssembly();
  void handleSecureRx();
  bool decryptSecureRecord();
  bool writeSecureRecord(const uint8_t *buffer, size_t size);
  bool writeClientAll(const uint8_t *buffer, size_t size);
  void makeSecureNonce(const uint8_t prefix[ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE],
                       uint64_t sequence, uint8_t nonce[12]) const;

  uint16_t _port;
  WiFiServer _server;
  WiFiClient _client;
  bool _started;
  bool _protocolReady;
  HandshakeState _handshakeState;
  uint32_t _handshakeStartedAt;
  size_t _handshakeLength;
  char _handshakeBuffer[ESPNETWORKSERIAL_HANDSHAKE_MAX_LENGTH];

  char _authKey[ESPNETWORKSERIAL_AUTH_KEY_MAX_LENGTH + 1];
  size_t _authKeyLength;
  char _clientNonceHex[(ESPNETWORKSERIAL_AUTH_NONCE_SIZE * 2) + 1];
  char _serverNonceHex[(ESPNETWORKSERIAL_AUTH_NONCE_SIZE * 2) + 1];

  bool _secureMode;
  uint8_t _txKey[ESPNETWORKSERIAL_SECURE_KEY_SIZE];
  uint8_t _rxKey[ESPNETWORKSERIAL_SECURE_KEY_SIZE];
  uint8_t _txNoncePrefix[ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE];
  uint8_t _rxNoncePrefix[ESPNETWORKSERIAL_SECURE_NONCE_PREFIX_SIZE];
  uint64_t _txSequence;
  uint64_t _rxSequence;

  uint8_t _rxRecordHeader[ESPNETWORKSERIAL_SECURE_HEADER_SIZE];
  size_t _rxRecordHeaderLength;
  uint16_t _rxCipherLength;
  uint8_t _rxCipher[ESPNETWORKSERIAL_SECURE_MAX_RECORD];
  size_t _rxCipherReceived;
  uint8_t _rxTag[ESPNETWORKSERIAL_SECURE_TAG_SIZE];
  size_t _rxTagReceived;

  uint8_t _rxPlain[ESPNETWORKSERIAL_SECURE_MAX_RECORD];
  size_t _rxPlainLength;
  size_t _rxPlainOffset;
};

class ESPNetworkSerial : public Stream {
public:
  explicit ESPNetworkSerial(uint16_t port = ESPNETWORKSERIAL_DEFAULT_PORT);

  // Easy-mode lifecycle. The built-in TCP transport is managed internally.
  void begin();
  void end();
  void handle();

  // Add optional companion streams such as USB Serial. The internal network
  // transport is always present and is not removed by clearStreams().
  bool addStream(Stream &stream);
  bool removeStream(Stream &stream);
  void clearStreams();
  size_t streamCount() const;

  // Network/security controls are exposed directly on the facade so sketches
  // do not need to know about ESPNetworkSerialTCP.
  bool setAuthKey(const char *key);
  void clearAuthKey();
  bool authenticationEnabled() const;

  bool waitForConnection();
  bool waitForConnection(uint32_t timeoutMs);

  bool started() const;
  bool connected();
  uint16_t port() const;
  IPAddress remoteIP();

  // Advanced escape hatch for transport-specific work.
  ESPNetworkSerialTCP &tcp();
  const ESPNetworkSerialTCP &tcp() const;

  using Print::write;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  int available() override;
  int read() override;
  size_t read(uint8_t *buffer, size_t size);
  int peek() override;
  void flush() override;

private:
  ESPNetworkSerialMux _mux;
  ESPNetworkSerialTCP _network;
};

extern ESPNetworkSerial ESPSerial;
