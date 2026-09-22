# ESPNetworkSerial Protocol

> Status: **experimental protocol v1 / not frozen**. The v1 handshake is implemented so the host can verify that TCP port 3233 is actually an ESPNetworkSerial endpoint before raw serial bytes are bridged.

## Separation of concerns

Arduino's Pluggable Monitor protocol is used only between Arduino IDE and the host monitor. ESPNetworkSerial protocol is used only between the host monitor and the ESP32.

## v1 connection handshake

The host opens TCP port `3233` and immediately sends:

~~~text
ESPNS/1 HELLO
~~~

terminated by LF.

The ESP32 responds:

~~~text
ESPNS/1 OK auth=none mode=raw
~~~

The host accepts `ESPNS/1 OK` followed by optional space-separated capability tokens.

Only after this exchange succeeds does the connection enter raw bidirectional stream mode.

### Why a handshake exists

Raw TCP alone cannot distinguish an ESPNetworkSerial endpoint from an unrelated service accidentally running on the same address/port. The handshake gives us:

- protocol/version identification;
- a clean place for capability negotiation;
- a clean extension point for authentication;
- deterministic failure when the wrong service is contacted.

The ESP32 handshake parser is non-blocking and times out after 2.5 seconds.

## Raw stream mode

After the handshake, bytes are not framed or transformed. Payload bytes from the sketch are forwarded as-is to the host and bytes from the host are exposed through the Arduino `Stream` API.

This keeps the hot data path small and preserves normal Serial semantics.

## Reconnect semantics

The host may reconnect to the same address after a temporary disconnect. Every new TCP connection performs a fresh ESPNS handshake before payload forwarding resumes.

The current host implementation keeps the Arduino IDE-side monitor session alive for a short reconnect grace period.

## Security

Protocol v1 currently reports:

~~~text
auth=none
~~~

That means endpoint identification is present, but authentication and confidentiality are **not**. Use the current prototype only on a trusted LAN.

Authentication will extend the handshake rather than being embedded in application serial payload.

## Still to be specified before v1 is frozen

- authenticated handshake and credential storage;
- encryption / TLS strategy;
- formal capability registry;
- discovery metadata specific to ESPNetworkSerial;
- keepalive behavior;
- protocol error codes;
- compatibility rules for future major/minor wire versions;
- maximum control-line sizes beyond the current implementation limits.
