# ESPNetworkSerial Protocol

> Status: **ESPNS/1 freeze candidate**. Endpoint identification, optional mutual HMAC-SHA256 authentication, and the authenticated AES-256-GCM record layer are implemented and hardware-tested.

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

## AES-256-GCM record layer

Authenticated serial traffic is split into records with a maximum plaintext payload of 1024 bytes.

~~~text
+----------------+-------------------+----------------------+----------------+
| length (2 BE)  | sequence (8 BE)   | ciphertext (length)  | GCM tag (16)   |
+----------------+-------------------+----------------------+----------------+
~~~

The 10-byte `length || sequence` header is authenticated as AES-GCM AAD.

The 96-bit GCM nonce is:

~~~text
direction_nonce_prefix (4 bytes) || sequence (8 bytes, big endian)
~~~

Sequence numbers start at zero independently in each direction and must increase by exactly one record at a time.

A record is rejected if the payload length is zero or exceeds 1024 bytes, the sequence is not the expected next value, or the GCM tag is invalid.

TCP segmentation is irrelevant: implementations buffer partial ESPNS headers, ciphertext and tags until a complete record is available.

## Reconnect semantics

A reconnect is a **new ESPNS session** with fresh client/server nonces, fresh HKDF-derived keys and nonce prefixes, and sequence numbers reset to zero.

The host monitor may keep the Arduino IDE-side monitor session alive while it creates the new ESPNS session. Secure-record state is never resumed across TCP connections.

## ESPNS/1 compatibility rules

ESPNS uses the integer in `ESPNS/1` as the wire-protocol major version.

For ESPNS/1:

- peers use the exact `ESPNS/1` command prefix;
- existing command names, required fields, proof strings, HKDF labels, record layout, nonce construction, and sequence semantics are stable;
- control-line fields are space-separated `name=value` tokens and are order-independent;
- receivers must ignore unknown optional `name=value` fields on otherwise recognized ESPNS/1 control lines;
- future ESPNS/1 extensions may add optional fields or new error codes, but may not make a new field mandatory for an existing successful flow;
- existing field values must not be silently reinterpreted;
- unknown authentication methods or data modes may be rejected;
- changes to cryptographic proofs, key derivation, record framing, nonce/sequence rules, or required handshake flow require a new major version such as `ESPNS/2`.

There is intentionally no wire-level minor version in this freeze candidate.

## Stable ESPNS/1 handshake error codes

| Code | Meaning |
| --- | --- |
| `bad_hello` | First control line is not a valid ESPNS/1 HELLO command. |
| `nonce_required` | Authentication is enabled but HELLO omitted the client nonce. |
| `invalid_nonce` | A nonce has the wrong length or invalid hexadecimal. |
| `bad_auth` | The expected AUTH control line is malformed or replaced by another command. |
| `auth_failed` | The client proof is missing, malformed, or incorrect. |
| `auth_internal` | The device could not compute the authentication challenge. |
| `secure_internal` | The device could not derive secure session material. |
| `line_too_long` | A handshake line exceeded the implementation limit. |
| `timeout` | The handshake did not complete before the timeout. |

Future ESPNS/1 implementations may add error codes, but the meanings above remain stable. Once secure mode is active, record-layer failures are fail-closed by dropping the connection rather than sending plaintext errors.

## Current limits

- handshake timeout: 2500 ms;
- maximum handshake control line: 256 bytes;
- nonce size: 16 bytes / 128 bits;
- HMAC: SHA-256;
- authenticated data mode: AES-256-GCM;
- secure record maximum plaintext: 1024 bytes;
- GCM authentication tag: 16 bytes;
- authentication key: 16..128 bytes.

## Host authentication configuration

Development builds look for `config.json` next to the monitor executable:

~~~json
{
  "authKey": "",
  "allowUnauthenticated": false
}
~~~

Environment overrides are available through `ESPNS_AUTH_KEY`, `ESPNS_ALLOW_UNAUTHENTICATED`, and `ESPNS_CONFIG`.

## Deliberately deferred beyond the first freeze

These do not need to block ESPNS/1:

- per-device key selection;
- richer capability negotiation;
- ESPNetworkSerial-specific discovery metadata;
- application-level keepalive behavior;
- key provisioning and rotation workflows;
- alternative secure modes or tuned record sizes.

Any deferred feature that requires an incompatible wire change belongs in ESPNS/2.
