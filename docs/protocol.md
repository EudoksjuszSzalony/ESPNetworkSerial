# ESPNetworkSerial Protocol

> Status: **experimental protocol v1 / not frozen**. Endpoint identification and optional mutual HMAC-SHA256 authentication are implemented. The raw serial data channel is not encrypted.

## Separation of concerns

Arduino's Pluggable Monitor protocol is used only between Arduino IDE and the host monitor. ESPNetworkSerial protocol is used only between the host monitor and the ESP32.

## Common connection start

The host opens TCP port `3233`, generates a random 128-bit client nonce, and sends:

~~~text
ESPNS/1 HELLO nonce=<32 lowercase hex characters>
~~~

Control messages are UTF-8/ASCII text lines terminated by LF. CR before LF is ignored.

The ESP32 then chooses one of the supported authentication paths.

## Unauthenticated path

If the ESP32 has no ESPNS authentication key configured, it responds:

~~~text
ESPNS/1 OK auth=none mode=raw
~~~

The connection immediately enters raw stream mode.

If the host has a key configured and does not explicitly allow unauthenticated endpoints, the host rejects this response as a downgrade.

## HMAC-SHA256 authenticated path

If the ESP32 has an authentication key configured, it requires the client nonce from `HELLO`, generates its own fresh 128-bit server nonce, and sends:

~~~text
ESPNS/1 CHALLENGE auth=hmac-sha256 nonce=<server_nonce> proof=<server_proof> mode=raw
~~~

The server proof is the lowercase hexadecimal representation of:

~~~text
HMAC-SHA256(key, "ESPNS/1 SERVER <client_nonce> <server_nonce>")
~~~

The host verifies the server proof. If valid, it replies:

~~~text
ESPNS/1 AUTH proof=<client_proof>
~~~

where the client proof is:

~~~text
HMAC-SHA256(key, "ESPNS/1 CLIENT <client_nonce> <server_nonce>")
~~~

The ESP32 verifies the proof using a constant-time comparison.

On success it replies:

~~~text
ESPNS/1 OK auth=hmac-sha256 mode=raw
~~~

Only then does the connection enter raw serial stream mode.

## Errors

The ESP32 may return control errors such as:

~~~text
ESPNS/1 ERR bad_hello
ESPNS/1 ERR nonce_required
ESPNS/1 ERR invalid_nonce
ESPNS/1 ERR bad_auth
ESPNS/1 ERR auth_failed
ESPNS/1 ERR auth_internal
ESPNS/1 ERR line_too_long
ESPNS/1 ERR timeout
~~~

Error codes are experimental and may change before v1 is frozen.

## Handshake limits

Current firmware defaults:

- handshake timeout: 2500 ms;
- maximum control-line buffer: 256 bytes;
- nonce size: 16 bytes / 128 bits;
- HMAC: SHA-256;
- authentication key: 16..128 bytes.

Every new TCP connection performs a new handshake with fresh nonces, including automatic reconnects after an ESP32 reset.

## Raw stream mode

After a successful `OK`, bytes are not framed or transformed. Payload bytes from the sketch are forwarded as-is to the host and bytes from the host are exposed through the Arduino `Stream` API.

This keeps the hot data path small and preserves normal Serial semantics.

The current raw data mode is neither encrypted nor integrity-protected.

## Why the handshake exists

The handshake provides:

- protocol/version identification;
- endpoint capability identification;
- optional mutual authentication;
- replay-resistant authentication through fresh nonces;
- a clean extension point for future security modes;
- deterministic failure when the wrong service is contacted.

## Reconnect semantics

The host may reconnect to the same address after a temporary disconnect. The current host monitor keeps the Arduino IDE-side monitor session alive for a short reconnect grace period.

A reconnect is a **new ESPNS session** and therefore performs a complete new handshake.

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

- encrypted/authenticated data transport;
- per-device key selection;
- formal capability registry;
- ESPNetworkSerial-specific discovery metadata;
- keepalive behavior;
- stable protocol error-code registry;
- compatibility rules for future major/minor wire versions;
- key provisioning and rotation.
