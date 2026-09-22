# Security

> Status: experimental. HMAC-SHA256 mutual authentication is implemented, but ESPNetworkSerial does **not** yet provide an encrypted or integrity-protected data channel.

ESPNetworkSerial carries console data and may allow commands to be sent back to the ESP32, so authentication is part of the protocol rather than an application-specific add-on.

## Current authentication mode

ESPNS v1 supports optional mutual authentication with a pre-shared key and HMAC-SHA256.

The shared key:

- is configured independently from Wi-Fi credentials;
- is independent from any ArduinoOTA password;
- is never transmitted over the network;
- must be between 16 and 128 bytes;
- should normally be a randomly generated 32-byte value.

The host monitor can generate a suitable key:

~~~powershell
espnetworkserial-monitor.exe --generate-key
~~~

The output is a 64-character hexadecimal string representing 32 random bytes. The string itself is used as the shared key, so the exact same text must be configured on both sides.

## Mutual challenge/response

For authenticated sessions the host and ESP32 each generate a fresh 128-bit random nonce.

The host sends:

~~~text
ESPNS/1 HELLO nonce=<client_nonce>
~~~

The device responds with its own nonce and a server proof:

~~~text
ESPNS/1 CHALLENGE auth=hmac-sha256 nonce=<server_nonce> proof=<server_proof> mode=raw
~~~

The server proof is:

~~~text
HMAC-SHA256(key, "ESPNS/1 SERVER <client_nonce> <server_nonce>")
~~~

The host verifies the proof before sending its own:

~~~text
ESPNS/1 AUTH proof=<client_proof>
~~~

where:

~~~text
HMAC-SHA256(key, "ESPNS/1 CLIENT <client_nonce> <server_nonce>")
~~~

The ESP32 verifies that proof with a constant-time comparison and only then enables the raw serial stream.

A fresh pair of nonces is generated for every TCP connection, including reconnects after reset.

## Downgrade behavior

If the host has an authentication key configured, an ESPNS endpoint that immediately offers:

~~~text
auth=none
~~~

is rejected by default.

This prevents an accidental or malicious downgrade from an authenticated configuration to an unauthenticated one.

The host configuration option:

~~~json
{
  "allowUnauthenticated": true
}
~~~

can deliberately relax this behavior for development environments that mix authenticated and unauthenticated boards.

## What this protects

The current HMAC handshake provides useful protection against:

- unauthorized clients opening an authenticated ESPNetworkSerial endpoint without the shared key;
- accidental connection to a different ESPNS device when authentication is expected;
- replaying a previously captured authentication proof against a fresh session;
- protocol downgrade when the host is configured to require authentication.

## What this does **not** protect

HMAC authentication is **not encryption**.

After authentication, the current `mode=raw` stream remains ordinary plaintext TCP. A network attacker able to observe or actively proxy the connection may still:

- read serial traffic;
- modify serial traffic;
- relay an authenticated handshake between the real host and device.

Therefore this version should still be considered suitable only for trusted LANs when serial contents or commands are sensitive.

A future secure transport must add confidentiality and stream integrity, most likely through TLS or an authenticated-encryption layer.

## Key storage

### ESP32

The BasicMonitor example reads `ESPNS_AUTH_KEY` from local `secrets.h`, which is ignored by Git.

The key is compiled into the firmware image. Anyone able to read the firmware/flash should therefore be assumed able to recover the key unless the platform uses additional flash/security protections.

### Host

During development, the monitor reads `config.json` from the directory containing the monitor executable. That file is ignored by Git.

The following environment variables are also supported:

- `ESPNS_AUTH_KEY`
- `ESPNS_ALLOW_UNAUTHENTICATED`
- `ESPNS_CONFIG`

No authentication key is printed to logs.

## Threat model still open before stable v1

- encrypted transport / payload integrity;
- per-device rather than one-default-key configuration;
- key rotation and provisioning;
- secure storage on supported ESP32 variants;
- brute-force / connection-rate limiting;
- discovery metadata that indicates ESPNS security capabilities without becoming a downgrade oracle;
- secure installer handling of host credentials.
