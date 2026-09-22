# ESPNetworkSerial

**Wireless Serial Monitor for ESP32, integrated directly with Arduino IDE.**

ESPNetworkSerial aims to make network serial feel like ordinary Arduino Serial: select your ESP32 network port, open Serial Monitor, and communicate bidirectionally over Wi-Fi — while keeping OTA available on the same device.

> **Status:** early development / pre-alpha. The firmware multiplexer, raw TCP transport, and first Arduino Pluggable Monitor host prototype are now in-tree. Native Arduino IDE integration is ready for development testing; the wire protocol and security layer are not final.

## Why

The project is built around one UX rule: **the sketch should not need duplicate log statements for USB and Wi-Fi.**

The firmware API now includes a `Print`/`Stream`-compatible multiplexer, so one call can fan out to multiple bidirectional streams:

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

## Current firmware prototype

The first implementation can already combine USB Serial with a TCP stream:

~~~cpp
#include <WiFi.h>
#include <ESPNetworkSerial.h>

ESPNetworkSerialTCP NetworkSerial;

void setup() {
    Serial.begin(115200);

    // Connect Wi-Fi first...

    NetworkSerial.begin();

    ESPSerial.addStream(Serial);
    ESPSerial.addStream(NetworkSerial);

    ESPSerial.println("Hello over USB and Wi-Fi!");
}

void loop() {
    NetworkSerial.handle();

    if (ESPSerial.available()) {
        int c = ESPSerial.read();
        // Input may come from USB Serial or the TCP client.
    }
}
~~~

`ESPSerial` broadcasts writes to all attached streams and reads from them using round-robin selection so one busy input does not permanently starve another.

The current TCP transport is deliberately only a **pre-alpha transport prototype**. It is plaintext and unauthenticated, so use it only on a trusted LAN. It is not yet the final Arduino IDE integration.

See [BasicMonitor](examples/BasicMonitor/BasicMonitor.ino) for the full example.

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

The host monitor is implemented in Go and speaks Arduino's Pluggable Monitor protocol over stdin/stdout. In development mode it bridges Arduino IDE's monitor connection to TCP port `3233` on the selected ESP32 network address.

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
- [Windows development setup](docs/development-setup.md)

User-oriented installation and Getting Started guides will be added as the first working version lands. GitHub Wiki can then provide the friendly how-to layer, while `docs/` remains the versioned technical source of truth.

## v0.1 milestone

- [x] Initial ESP32 `Stream` multiplexer
- [x] Initial bidirectional TCP transport prototype
- [x] Basic firmware example
- [x] Host-side Arduino Pluggable Monitor protocol prototype
- [ ] Native Arduino IDE Serial Monitor integration verified end-to-end
- [ ] Network port selected from Arduino IDE
- [ ] Arduino OTA + network monitor verified simultaneously in Arduino IDE
- [ ] Authentication / finalized protocol handshake
- [ ] Windows host binary / installer
- [x] Host monitor Go tests + cross-platform CI build workflow
- [ ] Signed/tagged release builds
- [ ] Protocol and extension-point stabilization

## License

MIT License. See [LICENSE](LICENSE).
