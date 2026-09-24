# ESPNetworkSerial

**Wireless Serial Monitor for ESP32, integrated directly with Arduino IDE.**

ESPNetworkSerial aims to make network serial feel like ordinary Arduino Serial: select your ESP32 network port, open Serial Monitor, and communicate bidirectionally over Wi-Fi — while keeping OTA available on the same device.

> **Status:** pre-release / ESPNS/1 freeze candidate. Native Arduino IDE monitoring, OTA coexistence, reconnect recovery, mutual HMAC-SHA256 authentication, AES-256-GCM encrypted transport, and real-device fault-injection testing are implemented. The project is preparing the v0.1 public API/protocol freeze.

## Why

The project is built around one UX rule: **the sketch should not need duplicate log statements for USB and Wi-Fi.**

The public firmware API is a `Print`/`Stream`-compatible facade. It owns the default network transport internally and can also fan the same data out to companion streams such as USB Serial:

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

## Current firmware API

The normal sketch only needs the single `ESPSerial` object:

~~~cpp
#include <ESPNetworkSerial.h>

void setup() {
    Serial.begin(115200);

    // Connect Wi-Fi first...

    // Optional companion stream: mirror the same RX/TX to USB Serial.
    ESPSerial.addStream(Serial);

    ESPSerial.begin();

    // Optional: wait up to 12 seconds so early boot logs can reach Wi-Fi Serial.
    ESPSerial.waitForConnection(12000);

    ESPSerial.println("Hello over USB and Wi-Fi!");
}

void loop() {
    ESPSerial.handle();

    if (ESPSerial.available()) {
        int c = ESPSerial.read();
        // Input may come from USB Serial or Arduino IDE over Wi-Fi.
    }
}
~~~

`ESPSerial` owns the TCP transport internally, broadcasts writes to the network plus all attached companion streams, and reads from them using round-robin selection so one busy input does not permanently starve another.

### Custom object name

If a project does not want the global `ESPSerial` object, instantiate the facade under any name:

~~~cpp
ESPNetworkSerial DebugSerial;

void setup() {
    DebugSerial.addStream(Serial);
    DebugSerial.begin();
    DebugSerial.println("Custom name, same API.");
}

void loop() {
    DebugSerial.handle();
}
~~~

Transport internals remain available as advanced APIs through `ESPNetworkSerialTCP`, `ESPNetworkSerialMux`, or `DebugSerial.tcp()`, but ordinary sketches do not need them.

### Boot-time wait modes

Waiting is optional:

~~~cpp
// no wait
// Do not call waitForConnection().

ESPSerial.waitForConnection(12000); // wait at most 12 seconds

ESPSerial.waitForConnection();      // required: wait indefinitely
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

## Optional authentication

ESPNetworkSerial can require mutual HMAC-SHA256 authentication before the serial stream opens.

Generate a development key:

~~~powershell
.\monitor\espnetworkserial-monitor.exe --generate-key
~~~

Put the same key in the sketch's local `secrets.h`:

~~~cpp
#define ESPNS_AUTH_KEY "PASTE_GENERATED_KEY_HERE"
~~~

and in local `monitor/config.json` (copy `monitor/config.example.json` first):

~~~json
{
  "authKey": "PASTE_GENERATED_KEY_HERE",
  "allowUnauthenticated": false
}
~~~

Both files containing local credentials are excluded from Git.

Authenticated sessions derive directional keys with HKDF-SHA256 and carry serial data in AES-256-GCM records, providing payload confidentiality and integrity. Unauthenticated sessions remain plaintext `mode=raw`. See [Security](docs/security.md) for guarantees and limitations.

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
    +-- ESPSerial facade
    |      +-- internal Network transport
    |      +-- optional USB Serial / other Streams
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

## Arduino Library Manager status

The development checkout can already appear under **File -> Examples** and **Sketch -> Include Library** because Arduino scans locally installed libraries. It is not expected to appear in the sidebar **Library Manager** catalog yet: that catalog is populated from Arduino's Library Manager registry/index.

ESPNetworkSerial will be submitted to the Arduino Library Manager registry after the first tagged public release and library metadata/API are stable enough to publish.

## Documentation


- [Architecture](docs/architecture.md)
- [Protocol specification](docs/protocol.md)
- [Security](docs/security.md)
- [Testing and hardening](docs/testing.md)
- [Firmware API stability](docs/api-stability.md)
- [Release process](docs/releases.md)
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
- [x] ESPNS v1 endpoint/version handshake
- [x] Arduino OTA + network monitor verified simultaneously during upload/reset
- [x] Optional mutual HMAC-SHA256 authentication handshake
- [x] Encrypted / integrity-protected AES-256-GCM serial transport
- [x] Secure stream torture tests (boundaries, tamper, replay, truncation, large bidirectional transfer)
- [x] Go race-detector CI
- [x] Real-device binary stress harness (`StressEcho` + `--stress`)
- [x] Authenticated 5 × 1 MiB real-device stress run with byte-for-byte verification
- [x] Non-blocking bulk-read path for encrypted records
- [x] 64 MiB single-session encrypted hardware torture run
- [x] 100 consecutive authenticated-session hardware torture run
- [x] Physical-reset fault injection with fresh authenticated-session recovery
- [x] Live Wi-Fi-loss fault injection with fresh authenticated-session recovery
- [x] StressEcho SW38 multi-click fault-test selector (stall / Wi-Fi drop / Wi-Fi off / TCP drop / restart)
- [x] Wi-Fi subsystem-off recovery with ESPNS/ArduinoOTA service restart
- [x] 8-second application stall survives without ESPNS reconnect
- [x] TCP-only disconnect recovers through a fresh authenticated session
- [ ] Windows end-user installer
- [ ] First tagged public release + Arduino Library Manager registration
- [x] Host monitor Go tests + cross-platform CI build workflow
- [x] Automated tagged multi-platform release builds + SHA-256 checksums
- [ ] Windows/macOS code signing and notarization
- [x] ESPNS/1 compatibility rules and stable handshake error registry
- [x] v0.1 facade/advanced API boundary documented
- [ ] Protocol and extension-point stabilization

Development testing has been confirmed with Arduino IDE 2.3.10 and ESP32 Arduino core 3.3.10, including OTA upload with an open Wi-Fi Serial Monitor and automatic monitor reconnection after the ESP32 reboots.

## License

MIT License. See [LICENSE](LICENSE).
