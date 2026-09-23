# ESPNetworkSerial Protocol

> Status: **experimental protocol v1 / not frozen**. Endpoint identification, optional mutual HMAC-SHA256 authentication, and an authenticated AES-256-GCM record layer are implemented.

## Separation of concerns

Arduino's Pluggable Monitor protocol is used only between Arduino IDE and the host monitor. ESPNetworkSerial protocol is used only between the host monitor and the ESP32.

## Common connection start

The host opens TCP port `3233`, generates a random 128-bit client nonce, and sends:

~~~text
ESPNS/1 HELLO nonce=<32 lowercase hex characters>
~~~

Control messages are UTF-8/ASCII text lines terminated by LF. CR before LF is ignored.

The ESP32 then chooses one of the supported security paths.

## Unauthenticated path

If the ESP32 has no ESPNS authentication key configured, it responds:

~~~text
ESPNS/1 OK auth=none mode=raw
~~~

The connection immediately enters raw plaintext stream mode.

If the host has a key configured and does not explicitly allow unauthenticated endpoints, the host rejects this response as a downgrade.

## Authenticated + encrypted path

If the ESP32 has an authentication key configured, it requires the client nonce from `HELLO`, generates its own fresh 128-bit server nonce, and sends:

~~~text
ESPNS/1 CHALLENGE auth=hmac-sha256 nonce=<server_nonce> proof=<server_proof> mode=aes256-gcm
~~~

The server proof is:

~~~text
HMAC-SHA256(
  key,
  "ESPNS/1 SERVER <client_nonce> <server_nonce> aes256-gcm"
)
~~~

The host verifies the proof. If valid, it replies:

~~~text
ESPNS/1 AUTH proof=<client_proof>
~~~

where:

~~~text
HMAC-SHA256(
  key,
  "ESPNS/1 CLIENT <client_nonce> <server_nonce> aes256-gcm"
)
~~~

The ESP32 verifies that proof using a constant-time comparison.

On success both sides derive session material and the ESP32 replies:

~~~text
ESPNS/1 OK auth=hmac-sha256 mode=aes256-gcm
~~~

The final `OK` line is still plaintext handshake traffic. Every following byte belongs to the encrypted record layer.

## Session key derivation

The configured authentication key is the HKDF input key material.

The HKDF-SHA256 salt is:

~~~text
client_nonce_raw || server_nonce_raw
~~~

where each nonce is 16 raw bytes decoded from its hexadecimal handshake form.

The implementation performs HKDF-Extract with HMAC-SHA256, then uses independent HKDF-Expand labels:

~~~text
ESPNS/1 aes256-gcm host-to-device key
ESPNS/1 aes256-gcm device-to-host key
ESPNS/1 aes256-gcm host-to-device nonce-prefix
ESPNS/1 aes256-gcm device-to-host nonce-prefix
~~~

The two AES keys are 32 bytes each. Each nonce prefix is 4 bytes.

This gives each direction a separate key and nonce space.

## AES-256-GCM record layer

Authenticated serial traffic is split into records with a maximum plaintext payload of 1024 bytes.

Each record is:

~~~text
+----------------+-------------------+----------------------+----------------+
| length (2 BE)  | sequence (8 BE)   | ciphertext (length)  | GCM tag (16)   |
+----------------+-------------------+----------------------+----------------+
~~~

The 10-byte `length || sequence` header is authenticated as AES-GCM additional authenticated data (AAD).

The 96-bit GCM nonce is:

~~~text
direction_nonce_prefix (4 bytes) || sequence (8 bytes, big endian)
~~~

Sequence numbers start at zero independently in each direction and must increase by exactly one record at a time.

A record is rejected if:

- the payload length is zero or exceeds 1024 bytes;
- the sequence number is not the expected next value;
- the AES-GCM authentication tag is invalid.

The sequence number is incremented only after successful encryption/decryption of a complete record.

TCP segmentation is irrelevant: implementations buffer partial ESPNS headers, ciphertext and tags until a complete record is available.

## Reconnect semantics

A reconnect is a **new ESPNS session**:

- fresh client nonce;
- fresh server nonce;
- fresh HKDF-derived directional keys;
- fresh nonce prefixes;
- sequence numbers reset to zero.

The host monitor may keep the Arduino IDE-side monitor session alive while it creates the new ESPNS session.

## Errors

The ESP32 may return plaintext handshake errors such as:

~~~text
ESPNS/1 ERR bad_hello
ESPNS/1 ERR nonce_required
ESPNS/1 ERR invalid_nonce
ESPNS/1 ERR bad_auth
ESPNS/1 ERR auth_failed
ESPNS/1 ERR auth_internal
ESPNS/1 ERR secure_internal
ESPNS/1 ERR line_too_long
ESPNS/1 ERR timeout
~~~

Once `mode=aes256-gcm` is active, malformed, replayed, out-of-order or unauthenticated records cause the connection to be closed instead of attempting to continue on a potentially desynchronized stream.

## Current limits

- handshake timeout: 2500 ms;
- maximum handshake control line: 256 bytes;
- nonce size: 16 bytes / 128 bits;
- HMAC: SHA-256;
- authenticated data mode: AES-256-GCM;
- secure record maximum plaintext: 1024 bytes;
- GCM authentication tag: 16 bytes;
- authentication key: 16..128 bytes.

## Why authenticated mode is framed

Plain serial semantics are preserved at the Arduino API boundary, but an authenticated-encryption algorithm needs explicit message boundaries, nonces and authentication tags.

The record layer is therefore internal. Sketch code still uses ordinary `Print`/`Stream` calls such as:

~~~cpp
ESPSerial.println("hello");
ESPSerial.available();
ESPSerial.read();
~~~

## Host authentication configuration

Development builds look for `config.json` next to the monitor executable:

~~~json
{
  "authKey": "",
  "allowUnauthenticated": false
}
~~~

`monitor/config.json` is ignored by Git.

Environment overrides are available through `ESPNS_AUTH_KEY`, `ESPNS_ALLOW_UNAUTHENTICATED`, and `ESPNS_CONFIG`.

## Still to be specified before v1 is frozen

- per-device key selection;
- formal capability registry;
- ESPNetworkSerial-specific discovery metadata;
- keepalive behavior;
- stable protocol error-code registry;
- compatibility rules for future major/minor wire versions;
- key provisioning and rotation;
- record-size/performance tuning.
