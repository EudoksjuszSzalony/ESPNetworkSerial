# ESPNetworkSerial Protocol

> Status: **not yet frozen**. This document defines goals and open design questions for protocol v1.

## Required capabilities

- Bidirectional byte transport.
- Clear connection lifecycle.
- Protocol version negotiation.
- Capability negotiation.
- Detectable authentication support.
- Defined error handling.
- Compatibility with multiple ESP32 devices on the same LAN.

## Separation of concerns

The wire protocol should not assume Arduino IDE internals. Arduino Pluggable Monitor communication belongs to the host-facing layer; ESPNetworkSerial protocol belongs between the host monitor and the device.

## Still to be specified

- Discovery record and service name.
- Handshake format.
- Framing versus raw stream mode.
- Capability identifiers.
- Authentication handshake.
- Reconnect semantics.
- Keepalive / timeout behavior.
- Maximum frame or buffer sizes.
- Protocol versioning rules.

No implementation should be considered protocol-stable until these items are specified and exercised by interoperability tests.
