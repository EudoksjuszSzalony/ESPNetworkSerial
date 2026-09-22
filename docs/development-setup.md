# Development setup: native Arduino IDE monitor on Windows

> Status: pre-alpha development workflow. This intentionally uses `platform.local.txt`, which Arduino documents as a development/beta mechanism for pluggable monitors.

This setup lets Arduino IDE use `ESPNetworkSerialMonitor` for ports whose protocol is `network`.

## 1. Build the host monitor

From PowerShell in the repository:

~~~powershell
.\monitor\build-windows.ps1
~~~

The script runs the Go tests and creates:

~~~text
monitor\espnetworkserial-monitor.exe
~~~

You need a recent Go toolchain in `PATH` for local builds.

## 2. Install the development monitor recipe

Close Arduino IDE, then run:

~~~powershell
.\installer\windows\install-dev.ps1
~~~

The script finds installed ESP32 Arduino core versions below `%LOCALAPPDATA%\Arduino15`, then adds a managed block to each core's `platform.local.txt`:

~~~text
# ESPNetworkSerial BEGIN
pluggable_monitor.pattern.network="D:/.../ESPNetworkSerial/monitor/espnetworkserial-monitor.exe"
# ESPNetworkSerial END
~~~

It refuses to overwrite a different existing `pluggable_monitor.pattern.network` recipe.

Restart Arduino IDE after installation.

### Optional: remove Arduino IDE's unnecessary OTA password prompt

For sketches that intentionally use ArduinoOTA **without a password**, development mode can install a second upload recipe that does not declare an upload password field:

~~~powershell
.\installer\windows\install-dev.ps1 -PromptlessOTA
~~~

After restarting Arduino IDE, network upload should start directly instead of showing the `Configure and Upload -> Password` dialog.

This is deliberately opt-in because Arduino chooses an upload tool by port protocol, not per-device `auth_upload` capability. While `-PromptlessOTA` is enabled, password-protected ArduinoOTA uploads in that ESP32 core will not work.

To restore the normal password-capable ESP32 OTA recipe while keeping ESPNetworkSerialMonitor installed, rerun:

~~~powershell
.\installer\windows\install-dev.ps1
~~~

## 3. Create local Wi-Fi credentials once

In:

~~~text
examples/BasicMonitor/
~~~

copy:

~~~text
secrets.example.h
~~~

to:

~~~text
secrets.h
~~~

and edit the copy:

~~~cpp
#define ESPNS_WIFI_SSID "your-ssid"
#define ESPNS_WIFI_PASSWORD "your-password"
~~~

`secrets.h` is ignored by Git, so pulling/updating the repository does not require re-entering credentials and the credentials are not committed accidentally.

## 4. Flash the BasicMonitor example over USB once

Open:

~~~text
examples/BasicMonitor/BasicMonitor.ino
~~~

The example starts both:

- ArduinoOTA, which advertises the ESP32 as Arduino's normal `network` port via mDNS;
- ESPNetworkSerial TCP on port `3233`.

By default the example waits up to 12 seconds for a Wi-Fi Serial Monitor connection before printing the main startup banner. This gives the monitor time to attach and capture early logs without permanently blocking an unattended board.

The firmware API supports three practical modes:

~~~cpp
// no wait
// do not call waitForConnection()

NetworkSerial.waitForConnection(12000); // timed wait

NetworkSerial.waitForConnection();      // required / indefinite wait
~~~

## 5. Select the ESP32 network port

After the board joins Wi-Fi, Arduino IDE should discover a network port such as:

~~~text
espnetworkserial-test at 192.168.1.128
~~~

Select that network port and open Serial Monitor.

The flow is:

~~~text
Arduino IDE
    |
    | Pluggable Monitor protocol (stdio + IDE callback TCP)
    v
ESPNetworkSerialMonitor.exe
    |
    | TCP 3233
    v
ESP32 ESPNetworkSerialTCP
~~~

The baud-rate control is intentionally irrelevant for this network transport; bytes are carried over TCP rather than a UART baud rate.

## 6. Bidirectional test

The example prints an uptime line every five seconds. Text sent from Arduino IDE Serial Monitor is read by `ESPSerial` and echoed back prefixed with `RX:`.

USB Serial stays enabled at the same time, so the same `ESPSerial.println(...)` output is visible over both transports.

## Reconnect test

With the Wi-Fi Serial Monitor open, reset the ESP32.

The host monitor keeps the Arduino IDE-side session alive and retries TCP port `3233` for up to 15 seconds. If the board comes back in that window, logging should resume in the same Serial Monitor tab.

If reconnection does not succeed before the grace period expires, the host emits `port_closed` and Arduino IDE may require the monitor to be reopened.

## Direct transport test

Before changing Arduino IDE integration, the host binary can be used as a plain TCP terminal bridge:

~~~powershell
.\monitor\espnetworkserial-monitor.exe --connect 192.168.1.128
~~~

Port `3233` is assumed unless an explicit `host:port` is supplied. Direct mode uses the same 15-second reconnect grace.

## Uninstall development integration

Close Arduino IDE and run:

~~~powershell
.\installer\windows\uninstall-dev.ps1
~~~

The script removes only the block managed by ESPNetworkSerial.

## Current limitations

- Pre-alpha raw TCP transport; no authentication or encryption yet.
- One network monitor client at a time.
- Reconnect currently retries the same discovered IP address; DHCP address changes during the reconnect window are not followed yet.
- The `network` -> monitor binding is installed per ESP32 core version; rerun the installer after a core update.
- `-PromptlessOTA` is intentionally a development-only override and applies to ESP32 network uploads for the installed core version, not only to one sketch.
- This development workflow is not the intended final end-user installer.
