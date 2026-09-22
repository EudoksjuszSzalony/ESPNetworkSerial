# ESPNetworkSerial

**Wireless Serial Monitor for ESP32, integrated directly with Arduino IDE.**

ESPNetworkSerial aims to make network serial feel like ordinary Arduino Serial: select your ESP32 network port, open Serial Monitor, and communicate bidirectionally over Wi-Fi — while keeping OTA available on the same device.

> **Status:** early development / pre-alpha. The public API and wire protocol are still being designed.

## Why

The project is built around one UX rule: **the sketch should not need duplicate log statements for USB and Wi-Fi.**

The target firmware API is a `Print`/`Stream`-compatible multiplexer so one call can fan out to multiple sinks:

~~~cpp
ESPSerial.println("Boot complete");
~~~

Conceptually:

~~~text
                 ESPSerial
                     |
          +----------+----------+
          |                     |
          v                     v
     USB Serial          Network Serial
                               |
                               v
                      Arduino Serial Monitor
~~~

## Project goals

- Native Arduino IDE Serial Monitor workflow over Wi-Fi.
- Bidirectional RX/TX.
- Arduino OTA and network serial working side by side.
- One simple `Print`/`Stream`-style API for USB + network output.
- Open protocol and open-source host monitor.
- No Python, Node.js, or .NET runtime required by end users.
- Standalone host binaries for Windows, Linux, and macOS.
- Transport/protocol architecture that can be extended without rewriting the monitor.

## Planned architecture

~~~text
ESP32 sketch
    |
    +-- ESPSerial multiplexer
    |      +-- USB Serial
    |      +-- Network transport
    |
    +-- OTA
           |
           v
      Network port
           |
           v
Arduino IDE <-> ESPNetworkSerialMonitor <-> ESP32
~~~

The host monitor is planned in Go and will implement Arduino's Pluggable Monitor protocol.

## Repository layout

~~~text
src/                 Arduino library source
examples/            Arduino examples
monitor/             Host monitor source
docs/                Versioned technical documentation
.github/             CI/release automation
library.properties   Arduino library metadata
~~~

The Arduino library metadata and `src/` directory live at the repository root so the project can later be distributed through Arduino Library Manager. Compiled host binaries will be published as release assets, not committed to the source tree.

## Documentation

- [Architecture](docs/architecture.md)
- [Protocol specification](docs/protocol.md)
- [Security](docs/security.md)
- [Adding another transport](docs/adding-a-transport.md)

User-oriented installation and Getting Started guides will be added as the first working version lands. GitHub Wiki can then provide the friendly how-to layer, while `docs/` remains the versioned technical source of truth.

## v0.1 milestone

- ESP32 firmware library
- Native Arduino IDE Serial Monitor integration
- Network port selected from Arduino IDE
- Bidirectional RX + TX
- Arduino OTA working simultaneously
- Windows host binary / installer
- Basic example sketch
- Documented protocol and extension points

## License

MIT License. See [LICENSE](LICENSE).
