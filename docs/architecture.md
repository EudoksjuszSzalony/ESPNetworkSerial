# Architecture

> Status: design draft.

ESPNetworkSerial is split into three independent layers.

## 1. Firmware library

Runs on ESP32 and exposes Arduino-style serial semantics through a public `ESPNetworkSerial` facade. The global `ESPSerial` convenience object owns the default network transport and automatically mirrors the same RX/TX to Arduino's default `Serial`, so ordinary sketches do not need duplicate log calls or manual stream registration. Custom facade instances keep companion streams explicit.

## 2. Network transport

Moves bytes between the ESP32 and the host monitor. The initial transport target is TCP. Discovery, transport, authentication, and stream semantics should remain separable where practical.

## 3. Host monitor

A standalone Go program that speaks Arduino's Pluggable Monitor protocol on the IDE side and ESPNS on the device side.

## Data flow

~~~text
Sketch -> ESPSerial multiplexer -> USB Serial
                           \----> Network transport -> Host monitor -> Arduino IDE

Arduino IDE -> Host monitor -> Network transport -> ESP32 RX
~~~

OTA is intentionally a sibling service rather than part of the serial data path. The end-user experience should allow the same discovered ESP32 network device to support both upload and monitoring.

## Design rule

Protocol- or transport-specific details must not leak through the whole codebase. A new transport should be implementable behind a small interface and registered with the host monitor.

## Firmware API layers

The firmware intentionally has two levels.

### Normal sketch API

~~~cpp
ESPSerial.begin();
ESPSerial.setAuthKey(...);
ESPSerial.waitForConnection(12000);
ESPSerial.println(...);
ESPSerial.handle();
~~~

A project may use its own object name instead:

~~~cpp
ESPNetworkSerial DebugSerial;
~~~

The complete facade API then follows that object name (`DebugSerial.begin()`, `DebugSerial.port()`, `DebugSerial.println()`, and so on).

### Advanced transport API

`ESPNetworkSerialTCP` and `ESPNetworkSerialMux` remain available as advanced building blocks. The facade exposes its built-in TCP backend through `tcp()` as an escape hatch.

These advanced types are not part of the v0.1 facade-stability promise; their internals may evolve as additional transports are explored. The normal `ESPNetworkSerial` facade is the compatibility boundary for ordinary sketches.

This split keeps the common API transport-agnostic without removing extensibility.
