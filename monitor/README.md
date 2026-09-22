# ESPNetworkSerialMonitor

This directory will contain the host-side monitor used by Arduino IDE.

## Design targets

- Implement Arduino Pluggable Monitor communication.
- Connect Arduino IDE to an ESP32 network transport.
- Support bidirectional byte streams.
- Keep transport logic behind a small abstraction layer.
- Build as a standalone Go binary for Windows, Linux, and macOS.
- Keep compiled binaries out of the repository; publish them as release assets.

The first implementation target is TCP. Additional transports should be addable without changing the Arduino IDE-facing monitor layer.
