# Architecture

> Status: design draft.

ESPNetworkSerial is split into three independent layers.

## 1. Firmware library

Runs on ESP32 and exposes Arduino-style serial semantics. The main ergonomic goal is a multiplexer that can fan the same output to USB Serial and one or more network sinks without duplicate log calls.

## 2. Network transport

Moves bytes between the ESP32 and the host monitor. The initial transport target is TCP. Discovery, transport, authentication, and stream semantics should remain separable where practical.

## 3. Host monitor

A standalone program, planned in Go, that speaks Arduino's Pluggable Monitor protocol on the IDE side and the selected ESPNetworkSerial transport on the device side.

## Data flow

~~~text
Sketch -> ESPSerial multiplexer -> USB Serial
                           \----> Network transport -> Host monitor -> Arduino IDE

Arduino IDE -> Host monitor -> Network transport -> ESP32 RX
~~~

OTA is intentionally a sibling service rather than part of the serial data path. The end-user experience should allow the same discovered ESP32 network device to support both upload and monitoring.

## Design rule

Protocol- or transport-specific details must not leak through the whole codebase. A new transport should be implementable behind a small interface and registered with the host monitor.
