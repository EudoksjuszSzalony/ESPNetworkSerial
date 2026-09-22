# ESPNetworkSerial

**Wireless Serial Monitor for ESP32, integrated directly with Arduino IDE.**

ESPNetworkSerial aims to make network serial feel like ordinary Arduino Serial: select your ESP32 network port, open Serial Monitor, and communicate bidirectionally over Wi-Fi — while keeping OTA available on the same device.

> **Status:** early development / pre-alpha. The firmware multiplexer, raw TCP transport, native Arduino Pluggable Monitor host prototype, boot-time connection waiting, and short reconnect recovery are now in-tree. The wire protocol and security layer are not final.

## Why

The project is built around one UX rule: **the sketch should not need duplicate log statements for USB and Wi-Fi.**

The firmware API includes a `Print`/`Stream`-compatible multiplexer, so one call can fan out to multiple bidirectional streams:

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

The current implementation can combine USB Serial with the network stream:

~~~cpp
#include <ESPNetworkSerial.h>

ESPNetworkSerialTCP NetworkSerial;

void setup() {
    Serial.begin(115200);

    // Connect Wi-Fi first...

    NetworkSerial.begin();

    ESPSerial.addStream(Serial);
    ESPSerial.addStream(NetworkSerial);

    // Optional: wait up to 12 seconds so early boot logs can reach Wi-Fi Serial.
    NetworkSerial.waitForConnection(12000);

    ESPSerial.println("Hello over USB and Wi-Fi!");
}

void loop() {
    NetworkSerial.handle();

    if (ESPSerial.available()) {
        int c = ESPSerial.read();
        // Input may come from USB Serial or Arduino IDE over Wi-Fi.
    }
}
~~~

`ESPSerial` broadcasts writes to all attached streams and reads from them using round-robin selection so one busy input does not permanently starve another.

### Boot-time wait modes

Waiting is optional:

~~~cpp
// no wait
// Do not call waitForConnection().

NetworkSerial.waitForConnection(12000); // wait at most 12 seconds

NetworkSerial.waitForConnection();      // required: wait indefinitely
~~~

The no-argument form is deliberately blocking. Use the timed or no-wait mode if the board must continue running unattended.

### Local Wi-Fi credentials

The BasicMonitor example uses a local `secrets.h` file. Copy:

~~~text
examples/BasicMonitor/secrets.example.h
~~~

to:

~~~text
examples/BasicMonitor/secrets.h
~~~

and fill in your SSID/password once. `secrets.h` is ignored by Git.

## Reconnect behavior

The host monitor now keeps the Arduino IDE monitor session alive while the ESP32 temporarily disappears and retries the TCP connection for a short grace period (currently 15 seconds). This is intended to cover common board resets and brief Wi-Fi interruptions without forcing the Serial Monitor tab to be closed and reopened.

If the ESP32 cannot be reached before the grace period expires, the monitor reports the port as closed.

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
installer/           Development/future installer work
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

GitHub Wiki can provide the friendly how-to layer, while `docs/` remains the versioned technical source of truth.

## v0.1 milestone

- [x] Initial ESP32 `Stream` multiplexer
- [x] Initial bidirectional TCP transport prototype
- [x] Basic firmware example
- [x] Host-side Arduino Pluggable Monitor protocol prototype
- [x] Native Arduino IDE Serial Monitor integration verified end-to-end
- [x] Network port selected directly from Arduino IDE
- [x] Boot-time `waitForConnection()` API
- [x] Short automatic reconnect grace after ESP32 disconnect/reset
- [x] Local Git-ignored Wi-Fi credentials for examples
- [ ] Arduino OTA + network monitor verified simultaneously during upload/reset
- [ ] Authentication / finalized protocol handshake
- [ ] Windows end-user installer
- [x] Host monitor Go tests + cross-platform CI build workflow
- [ ] Signed/tagged release builds
- [ ] Protocol and extension-point stabilization

Development testing has been confirmed with Arduino IDE 2.3.10 and ESP32 Arduino core 3.3.10.

## License

MIT License. See [LICENSE](LICENSE).
