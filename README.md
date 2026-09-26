# ESPNetworkSerial

**Wireless Serial Monitor for ESP32, integrated directly with Arduino IDE.**

ESPNetworkSerial aims to make network serial feel like ordinary Arduino Serial: select your ESP32 network port, open Serial Monitor, and communicate bidirectionally over Wi-Fi — while keeping OTA available on the same device.

> **Status:** v0.1.0 is publicly released. The current v0.1.1 development line adds cross-platform setup packages and automatic machine-local authentication provisioning. Arduino Library Manager registration is planned after the new installers are validated.

## Why

The project is built around one UX rule: **the sketch should not need duplicate log statements for USB and Wi-Fi.**

The public firmware API is a `Print`/`Stream`-compatible facade. The built-in global `ESPSerial` is the zero-boilerplate path: `ESPSerial.begin()` starts the network transport, initializes Arduino's default `Serial` at 115200, and mirrors the same RX/TX locally and over Wi-Fi:

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
  Default Serial         Network Serial
                               |
                               v
                      Arduino Serial Monitor
~~~

## Current firmware API

The normal sketch only needs the single `ESPSerial` object:

~~~cpp
#include <ESPNetworkSerial.h>

void setup() {
    // Connect Wi-Fi first...

    // Starts ESPNS and automatically enables/mirrors Arduino Serial at 115200.
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

`ESPSerial` owns the TCP transport internally. The global object automatically includes Arduino's default `Serial`, so normal sketches do **not** need `Serial.begin(...)` or `ESPSerial.addStream(Serial)`. Writes go to local Serial and the network with one call; reads are selected fairly across available inputs. Define `ESPNETWORKSERIAL_GLOBAL_SERIAL_MIRROR=0` before including the library to opt out, or override `ESPNETWORKSERIAL_GLOBAL_SERIAL_BAUD` if 115200 is not appropriate.

### Custom object name

If a project does not want the global `ESPSerial` object, instantiate the facade under any name:

~~~cpp
ESPNetworkSerial DebugSerial;

void setup() {
    Serial.begin(115200);
    DebugSerial.addStream(Serial);
    DebugSerial.begin();
    DebugSerial.println("Custom name, same API.");
}

void loop() {
    DebugSerial.handle();
}
~~~

Custom `ESPNetworkSerial` instances intentionally stay explicit and do not automatically claim the global Arduino `Serial`; this avoids surprising libraries or sketches that need different stream ownership. Transport internals remain available as advanced APIs through `ESPNetworkSerialTCP`, `ESPNetworkSerialMux`, or `DebugSerial.tcp()`, but ordinary sketches do not need them.

## Examples

- **BasicMonitor** — monitor-only example. No ArduinoOTA service is started. It advertises the ESPNS endpoint through mDNS so Arduino IDE can discover the board as a network port for Serial Monitor.
- **WirelessOTAAndMonitor** — adds ArduinoOTA to the same device and demonstrates OTA + ESPNetworkSerial coexistence. Ordinary application logs use `ESPSerial`; OTA progress/error callbacks intentionally use local `Serial` only to avoid extra traffic during the upload.
- **CustomInstance** — demonstrates an explicitly named `ESPNetworkSerial` object and manual companion-stream selection.
- **StressEcho** — transport torture/fault-injection firmware used for hardware validation.

> In **BasicMonitor**, mDNS discovery is for monitoring only; network OTA upload is not enabled by that sketch. Use **WirelessOTAAndMonitor** when OTA upload is required.

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

## Setup-provisioned authentication

The end-user setup can provision secure ESPNS authentication automatically. On the first install, if no host `config.json` exists, Setup generates a random 256-bit key, stores it in the host config, and writes an installer-managed `ESPNetworkSerialConfig.h` into every detected ESP32 Arduino core.

That generated header defines:

~~~cpp
#define ESPNS_DEFAULT_AUTH_KEY "..."
~~~

`ESPNetworkSerial.h` imports the header automatically when it is available on the ESP32 core include path. Normal sketches therefore do not need to copy the key manually.

Authentication priority is:

~~~text
setAuthKey(...) called before begin()
        ↓
ESPNS_AUTH_KEY defined by the sketch
        ↓
ESPNS_DEFAULT_AUTH_KEY provisioned by Setup
        ↓
no key -> auth=none / mode=raw
~~~

Define `ESPNS_DISABLE_DEFAULT_AUTH_KEY` before including `ESPNetworkSerial.h` to deliberately ignore the machine-local default. A sketch-specific `ESPNS_AUTH_KEY` always takes priority over the generated default.

Compile-time overrides are sketch-local, so `ESPNS_AUTH_KEY` and `ESPNS_DISABLE_DEFAULT_AUTH_KEY` must be defined **before** `#include <ESPNetworkSerial.h>`. The bundled examples include `secrets.h` first for exactly this reason.

Repair operations reuse the existing host key. Key regeneration is an explicit operation because rotating it requires previously compiled ESP32 firmware to be rebuilt/reflashed.

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
- End-user setup automation for Windows plus terminal-based Linux/macOS installation.
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
installer/           Windows GUI installer + Linux/macOS terminal setup
docs/                Versioned technical documentation
.github/             CI/release automation
library.properties   Arduino library metadata
~~~

The Arduino library metadata and `src/` directory live at the repository root so the project can later be distributed through Arduino Library Manager. Compiled host binaries will be published as release assets, not committed to the source tree.

## Arduino Library Manager status

The development checkout can already appear under **File -> Examples** and **Sketch -> Include Library** because Arduino scans locally installed libraries. It is not expected to appear in the sidebar **Library Manager** catalog yet: that catalog is populated from Arduino's Library Manager registry/index.

ESPNetworkSerial v0.1.0 is tagged and publicly released. Library Manager submission is intentionally waiting for the cross-platform setup/auth-provisioning work to be validated so installing the Arduino library does not leave Linux/macOS users without an IDE monitor integration path.

## Documentation


- [Architecture](docs/architecture.md)
- [Protocol specification](docs/protocol.md)
- [Security](docs/security.md)
- [Testing and hardening](docs/testing.md)
- [Firmware API stability](docs/api-stability.md)
- [Release process](docs/releases.md)
- [Windows end-user installer](docs/windows-installer.md)
- [Linux/macOS terminal installer](docs/unix-installer.md)
- [v0.1 release checklist](docs/v0.1-release-checklist.md)
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
- [x] Windows end-user installer build + Arduino core integration tooling
- [x] Windows end-user installer end-to-end validation on a normal Windows environment
- [x] First tagged public release (`v0.1.0`)
- [ ] Arduino Library Manager registration
- [x] Linux/macOS terminal setup implementation + CI
- [ ] Linux/macOS end-to-end installer validation on native user machines
- [x] Host monitor Go tests + cross-platform CI build workflow
- [x] Automated tagged multi-platform release builds + SHA-256 checksums
- [ ] Windows/macOS code signing and notarization
- [x] ESPNS/1 compatibility rules and stable handshake error registry
- [x] v0.1 facade/advanced API boundary documented
- [x] v0.1 protocol/public-API compatibility boundary stabilized; advanced transport extension APIs explicitly remain experimental

Development testing has been confirmed with Arduino IDE 2.3.10 and ESP32 Arduino core 3.3.10, including OTA upload with an open Wi-Fi Serial Monitor and automatic monitor reconnection after the ESP32 reboots.

## License

MIT License. See [LICENSE](LICENSE).
