<div align="center">

# ESPNetworkSerial

**Use the Arduino IDE Serial Monitor over Wi-Fi — with the familiar `Serial` experience.**

Bidirectional communication · USB + Wi-Fi mirroring · Secure transport · ArduinoOTA support

[![Latest release](https://img.shields.io/github/v/release/EudoksjuszSzalony/ESPNetworkSerial?label=release)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/latest)
[![Host monitor CI](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/actions/workflows/monitor-ci.yml/badge.svg)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/actions/workflows/monitor-ci.yml)
[![Firmware CI](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/actions/workflows/firmware-ci.yml/badge.svg)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/actions/workflows/firmware-ci.yml)
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[**Download**](#-download) · [**Quick start**](#-quick-start) · [**OTA authentication**](#-ota-authentication) · [**Documentation**](#-documentation)

</div>

---

## What is ESPNetworkSerial?

ESPNetworkSerial brings **Arduino IDE's built-in Serial Monitor to ESP32 over Wi-Fi**. Select the discovered network port and monitor your device without leaving a USB cable connected.

A single `ESPSerial.print()` call can write to both the physical USB serial stream and the network monitor. The same interface also accepts input from either side.

```text
                              ESP32
                    ┌─────────────────────┐
                    │       ESPSerial     │
                    └──────────┬──────────┘
                         ┌─────┴─────┐
                         ▼           ▼
                     USB Serial    Wi-Fi TCP :3233
                         │           │
                         ▼           ▼
                    Local monitor  ESPNS host monitor
                                     │
                                     ▼
                             Arduino IDE Serial Monitor
```

**Highlights**

- **Native Arduino IDE workflow** — uses the existing Serial Monitor and discovered network ports.
- **One `Print` / `Stream` interface** — `print`, `println`, `printf`, `write`, `available`, `read`, `peek`, and `flush`.
- **USB + Wi-Fi mirroring** with one call; no duplicate logging statements.
- **Bidirectional data** — input from local Serial or the network monitor.
- **Authenticated, encrypted transport** — mutual HMAC-SHA256 authentication with AES-256-GCM data records when a key is configured.
- **Automatic reconnect grace** — the host monitor tolerates short resets or Wi-Fi interruptions.
- **ArduinoOTA alongside the monitor**, including an optional installer-provisioned OTA credential.
- **Standalone host binaries** — no Go toolchain needed for end users.

## 📥 Download

**Latest published stable release: [v0.1.2](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/tag/v0.1.2).** The links below point to actual assets in that release.

| Operating system | Setup download | Package |
|:--|:--|:--|
| **Windows 10/11 · x64** | **[⬇ Download Windows installer (.exe)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/download/v0.1.2/espnetworkserial-setup-0.1.2-windows-amd64.exe)** | Graphical installer |
| **Linux · x64** | [⬇ Download Linux x64 (.tar.gz)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/download/v0.1.2/espnetworkserial-setup-0.1.2-linux-amd64.tar.gz) | Terminal setup |
| **Linux · ARM64** | [⬇ Download Linux ARM64 (.tar.gz)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/download/v0.1.2/espnetworkserial-setup-0.1.2-linux-arm64.tar.gz) | Terminal setup |
| **macOS · Intel** | [⬇ Download macOS Intel (.tar.gz)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/download/v0.1.2/espnetworkserial-setup-0.1.2-macos-amd64.tar.gz) | Terminal setup |
| **macOS · Apple Silicon** | [⬇ Download macOS Apple Silicon (.tar.gz)](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/download/v0.1.2/espnetworkserial-setup-0.1.2-macos-arm64.tar.gz) | Terminal setup |

[**Browse the latest release and all assets →**](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/latest)  
[Verify downloads with SHA-256 checksums](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/download/v0.1.2/SHA256SUMS.txt) · [All releases](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases)

> **Release note:** The two-choice OTA authentication installer page described [below](#-ota-authentication) is currently under development in [PR #3](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/pull/3) and **is not part of published v0.1.2**. For an experimental build, use [Actions → Release](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/actions/workflows/release.yml), select `feature/config-backed-secure-ota`, run the workflow, and download its Windows installer artifact. Workflow artifacts are temporary, so they are not used as permanent download links here.

### Install the host integration

<details>
<summary><strong>🪟 Windows — graphical setup</strong></summary>

1. Download and run the Windows `.exe` from the table above. Administrator privileges are not normally required.
2. Let setup register the ESPNS host monitor with detected Arduino ESP32 cores and provision its authentication key.
3. **Completely restart Arduino IDE** to reload the updated network monitor integration.
4. If you install/update the ESP32 board package later, use **Start → ESPNetworkSerial → Repair Arduino integration**.

Default install directory: `%LOCALAPPDATA%\Programs\ESPNetworkSerial`.

The installer is currently unsigned; Windows may display a publisher warning. Check the file's SHA-256 hash against the release checksum file.

[Detailed Windows instructions →](docs/windows-installer.md)

</details>

<details>
<summary><strong>🐧 Linux / 🍎 macOS — terminal setup</strong></summary>

1. Download the setup `.tar.gz` matching your operating system and CPU.
2. Extract it, open a terminal in the extracted folder, and run:

```bash
./install.sh
```

3. Restart Arduino IDE.

To repair the integration after installing a new ESP32 core, run the installed `install.sh --repair`. A normal end-user setup does not require building Go from source.

[Detailed Linux/macOS instructions →](docs/unix-installer.md)

</details>

> The **Arduino library** and the **host setup** are separate components. Install the library through **Arduino IDE → Library Manager → search `ESPNetworkSerial`**, or use a checkout of this repository. The host setup is still required to enable the IDE's wireless Serial Monitor.

## 🚀 Quick start

### 1. Open the included example

In Arduino IDE, go to **File → Examples → ESPNetworkSerial → BasicMonitor**.

Copy `examples/BasicMonitor/secrets.example.h` to `secrets.h` in the example folder and enter your Wi-Fi SSID/password. Do not commit `secrets.h` to Git.

### 2. Upload once over USB

Select your ESP32 board and its **USB serial port**. Upload `BasicMonitor`. The sketch joins Wi-Fi and advertises its ESPNS monitor endpoint with mDNS.

### 3. Open the wireless Serial Monitor

After the ESP32 connects:

```text
Arduino IDE
  → Tools → Port → select the discovered ESP32 network port
  → Serial Monitor
  → read logs and send data over Wi-Fi ✨
```

`BasicMonitor` is for **monitoring only**. For network firmware uploads too, use the `WirelessOTAAndMonitor` example, which starts ArduinoOTA as well.

### 4. Use `ESPSerial` in your own sketch

Connect Wi-Fi before starting ESPNS. The global `ESPSerial` automatically initializes and mirrors the default USB `Serial` at 115200 baud.

```cpp
#include <WiFi.h>
#include <ESPNetworkSerial.h>

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  ESPSerial.begin();
  ESPSerial.println("Hello over USB and Wi-Fi!");
}

void loop() {
  ESPSerial.handle();

  if (ESPSerial.available()) {
    const int byteReceived = ESPSerial.read();
    if (byteReceived >= 0) ESPSerial.write((uint8_t)byteReceived);
  }
}
```

This minimal sketch does **not** advertise its own mDNS service. Use the `BasicMonitor` example's `ESPmDNS` setup for automatic discovery in Arduino IDE.

**Need your own Serial object?** `ESPNetworkSerial DebugSerial;` is supported. Custom instances manage their attached streams explicitly (for example, `DebugSerial.addStream(Serial)`).

**Important:** `ESPSerial.waitForConnection()` without a timeout blocks until a monitor connects. Use a timeout (for example, `ESPSerial.waitForConnection(12000)`) or skip it on unattended devices.

## 🔐 OTA authentication

ESPNetworkSerial's **network Serial Monitor encryption** and **ArduinoOTA firmware upload authentication** are related but separate mechanisms.

The in-development Windows installer offers two ways to authenticate ArduinoOTA uploads:

| Mode | Arduino IDE behavior | Firmware requirement |
|:--|:--|:--|
| **Autogenerated ESPNS key** | Upload starts without an IDE password prompt. The host OTA wrapper supplies the installer-provisioned key. | `ArduinoOTA` must use the same provisioned key. |
| **Password** | The stock ESP32 OTA uploader asks for the password in Arduino IDE. | `ArduinoOTA.setPassword(...)` must use the user's chosen password. |

The selected mode is saved in `config.json` as `otaAuthMode` and exported to firmware through the generated `ESPNetworkSerialConfig.h`. Changing OTA mode does **not** change or rotate the network Serial Monitor key.

When installed from a build containing this feature, the Windows Start menu offers:

```text
ESPNetworkSerial
└── OTA authentication
    ├── Use autogenerated HMAC-SHA256 key
    └── Use Arduino IDE password
```

Changing modes updates the Arduino integration. Restart Arduino IDE and **rebuild/reflash firmware configured for the chosen mode** before expecting OTA authentication to succeed. See the [WirelessOTAAndMonitor example](examples/WirelessOTAAndMonitor/WirelessOTAAndMonitor.ino) for the corresponding firmware selection.

> The label **HMAC-SHA256** refers to the OTA authentication handshake, not AES-GCM-encrypted firmware transfer. OTA payload confidentiality is **not** implied. By contrast, authenticated ESPNetworkSerial monitor sessions encrypt their serial payload with AES-256-GCM. See [Security](docs/security.md) for exact guarantees and limitations.

## 🧩 How it fits together

```text
ESP32 firmware                              Your computer
┌────────────────────────┐                 ┌───────────────────────────┐
│ ESPSerial              │  ESPNS/1 TCP    │ ESPNetworkSerial Monitor  │
│ Print/Stream ↔ network ├────────────────►│ Arduino IDE integration   │
│            ↕ USB       │    port 3233    │                           │
│ ArduinoOTA (optional)  │◄────────────────┤ espota uploader (optional)│
└────────────────────────┘  OTA port 3232  └───────────────────────────┘
```

ESPNS authenticated sessions use mutual HMAC-SHA256 authentication, HKDF-derived session keys, and AES-256-GCM records. Without an authentication key, raw TCP is unencrypted and should be limited to trusted development environments.

The installer can generate a random 256-bit key once and provision the matching host `config.json` and firmware `ESPNS_DEFAULT_AUTH_KEY`. **Repair preserves the key; explicit regeneration rotates it.** After rotation, existing firmware must be rebuilt/reflashed to reconnect securely.

## 📚 Documentation

| Read this | When you need… |
|:--|:--|
| [Windows installer](docs/windows-installer.md) | Installation, repair, core upgrades, uninstall |
| [Linux and macOS setup](docs/unix-installer.md) | Terminal setup and repair |
| [Security model](docs/security.md) | Authentication, encryption, key management and limitations |
| [Firmware API stability](docs/api-stability.md) | Stable public API and advanced extension boundaries |
| [Architecture](docs/architecture.md) | Library/monitor structure |
| [ESPNS/1 protocol](docs/protocol.md) | Handshake, framing and transport |
| [Testing and hardening](docs/testing.md) | Stress tests and fault injection |
| [Release process](docs/releases.md) | Building packages and publishing tags |

### Examples

- [BasicMonitor](examples/BasicMonitor) — network Serial Monitor with mDNS, no OTA.
- [WirelessOTAAndMonitor](examples/WirelessOTAAndMonitor) — monitor plus OTA in one sketch.
- [CustomInstance](examples/CustomInstance) — separately named serial facade.
- [StressEcho](examples/StressEcho) — binary stress/fault-injection testing.

### Troubleshooting

**“No monitor available for the port protocol network”** means Arduino IDE does not have an active network monitor recipe for the selected ESP32 core. Completely close Arduino IDE, run **Repair Arduino integration** (Windows) or the installed `install.sh --repair` (Linux/macOS), then restart the IDE.

If OTA authentication fails, first verify that the **installed upload mode**, the **key/password compiled into firmware**, and the **host configuration** agree.

Found a bug or need another platform? [Open an issue](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/issues).

## 📄 License

[MIT License](LICENSE). Contributions and reproducible bug reports are welcome.

<div align="center">

[⬆ Back to top](#espnetworkserial) · [Download latest release](https://github.com/EudoksjuszSzalony/ESPNetworkSerial/releases/latest)

</div>
