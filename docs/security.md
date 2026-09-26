# Security

> Status: ESPNS/1 authenticated sessions provide mutual authentication, confidentiality and record integrity using HMAC-SHA256, HKDF-SHA256 and AES-256-GCM. The design has extensive automated and real-device regression testing but has not received an external security review.

ESPNetworkSerial carries console data and may allow commands to be sent back to the ESP32, so security is part of the transport rather than an application-specific add-on.

## Security modes

### No ESPNS key configured

The endpoint uses:

~~~text
auth=none mode=raw
~~~

This is ordinary plaintext TCP and should be treated as trusted-LAN development mode.

### ESPNS key configured

The endpoint uses:

~~~text
auth=hmac-sha256 mode=aes256-gcm
~~~

The connection performs mutual challenge/response authentication and then moves to an AES-256-GCM record layer.

## Shared key

The PSK:

- is configured independently from Wi-Fi credentials;
- is independent from any ArduinoOTA password;
- is never transmitted over the network;
- must be between 16 and 128 bytes;
- should normally be a randomly generated value.

The host monitor can generate a suitable key manually:

~~~powershell
espnetworkserial-monitor.exe --generate-key
~~~

End-user Setup can also provision this automatically. If no host `config.json` exists, Setup generates a random 256-bit key, stores it in the host config, and writes the same value to an installer-managed `ESPNetworkSerialConfig.h` in each detected ESP32 core as `ESPNS_DEFAULT_AUTH_KEY`.

Repair reuses the existing key. Rotation is explicit because firmware compiled before the rotation still contains the old PSK.

## Mutual authentication

The host and ESP32 each generate a fresh 128-bit random nonce for every TCP connection.

Server and client HMAC proofs bind:

- the ESPNS protocol version;
- the authentication role (SERVER or CLIENT);
- both fresh nonces;
- the negotiated `aes256-gcm` mode.

This authenticates both peers before encrypted serial data is accepted.

## Session key derivation

The PSK is not used directly as an AES key.

HKDF-SHA256 derives four independent pieces of session material from the PSK plus the fresh client/server nonces:

- host → device AES-256 key;
- device → host AES-256 key;
- host → device 32-bit nonce prefix;
- device → host 32-bit nonce prefix.

Every reconnect derives fresh material.

## Record protection

Each direction has an independent 64-bit sequence counter.

The AES-GCM nonce is:

~~~text
4-byte session nonce prefix || 8-byte sequence
~~~

The record header containing payload length and sequence number is authenticated as GCM AAD.

As a result, authenticated mode detects:

- ciphertext modification;
- header modification;
- forged records;
- repeated records;
- reordered records;
- skipped sequence numbers within the same connection.

Invalid secure records terminate the connection.

## Downgrade behavior

If the host has an authentication key configured, an endpoint offering `auth=none mode=raw` is rejected by default.

The host can deliberately relax this during development with:

~~~json
{
  "allowUnauthenticated": true
}
~~~

Authenticated endpoints themselves do not fall back from `aes256-gcm` to authenticated plaintext.

## What authenticated mode protects

Against an attacker who can observe or modify LAN traffic but does not know the PSK, authenticated mode is designed to provide:

- mutual peer authentication;
- serial-payload confidentiality;
- serial-record integrity;
- replay/out-of-order detection within a session;
- downgrade rejection when the host requires authentication.

A passive observer still sees metadata such as IP addresses, TCP timing and approximate encrypted record sizes.

## Important limitations

### No forward secrecy

This is PSK-based security, not an ephemeral Diffie-Hellman exchange.

If an attacker records encrypted traffic and later obtains the PSK, the recorded handshake nonces are sufficient to derive those historical session keys.

### Denial of service is not prevented

An attacker can still drop packets, reset TCP connections, flood the listening port or otherwise make the service unavailable.

### Relay attacks are not device identity beyond the PSK

Any device possessing the same PSK is part of the same trust domain. The current protocol does not yet bind a unique device certificate/identity to a particular board.

### Key extraction from firmware

The BasicMonitor example compiles the PSK into firmware. Anyone able to read unprotected flash should be assumed able to recover it.

ESP32 flash encryption / secure boot are separate platform-security topics and are not enabled by this library.

### Custom protocol without external audit

The implementation uses standard primitives, but the ESPNS composition, framing and implementation have not been externally audited. Do not treat the current release as a substitute for a reviewed production security protocol where that level of assurance is required.

## Key storage

### ESP32

A sketch can provide `ESPNS_AUTH_KEY` directly, or call `setAuthKey(...)` before `begin()`.

When Setup provisioning is used, the machine-local key is copied into an installer-managed:

~~~text
<ESP32 core>/cores/esp32/ESPNetworkSerialConfig.h
~~~

as:

~~~cpp
#define ESPNS_DEFAULT_AUTH_KEY "..."
~~~

The firmware key priority is:

~~~text
setAuthKey(...) before begin()
        ↓
ESPNS_AUTH_KEY
        ↓
ESPNS_DEFAULT_AUTH_KEY
~~~

`ESPNS_DISABLE_DEFAULT_AUTH_KEY` disables only the installer-provided default. The key is still compiled into firmware and should be considered recoverable by anyone able to read unprotected flash.

### Host

The installed monitor reads `config.json` next to its executable. Setup-created configs use restrictive file permissions where supported and are not printed to the terminal.

Environment overrides:

- `ESPNS_AUTH_KEY`
- `ESPNS_ALLOW_UNAUTHENTICATED`
- `ESPNS_CONFIG`

No authentication key is intentionally written to monitor logs.

## Open security work

- external protocol/security review;
- per-device keys instead of one default key;
- key rotation/provisioning;
- secure host credential storage;
- ESP32 secure key storage;
- brute-force / connection-rate limiting;
- authenticated device identity beyond possession of the PSK;
- optional forward secrecy.
