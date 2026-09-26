# ESPNetworkSerialMonitor

The host-side bridge used by Arduino IDE.

## Host monitor

The monitor is implemented in Go using only the standard library. It implements Arduino Pluggable Monitor protocol v1 commands:

- `HELLO`
- `DESCRIBE`
- `CONFIGURE`
- `OPEN`
- `CLOSE`
- `QUIT`

For `network` ports, `OPEN` connects to the selected ESP32 address on TCP port `3233` and bridges bytes bidirectionally to Arduino IDE's callback connection.

## ESPNS authentication

The monitor supports mutual HMAC-SHA256 authentication.

For manual configuration, generate a random 32-byte key:

~~~powershell
.\espnetworkserial-monitor.exe --generate-key
~~~

Copy `config.example.json` to `config.json` next to the executable and place the generated text in `authKey`.

End-user Setup uses provisioning commands instead:

~~~text
--provision-auth
--regenerate-auth
--write-firmware-config <ESPNetworkSerialConfig.h>
~~~

`--provision-auth` creates a random 256-bit key only when no config exists. Repair therefore reuses the existing key. `--regenerate-auth` deliberately rotates it. `--write-firmware-config` copies the persistent host key into an installer-managed firmware header as `ESPNS_DEFAULT_AUTH_KEY` without printing the key to the terminal.

A sketch-defined `ESPNS_AUTH_KEY` or a programmatic `setAuthKey(...)` can override the installer-provisioned default. Sketch-local compile-time macros must be defined before `#include <ESPNetworkSerial.h>`.

When a host key is configured, an `auth=none` endpoint is rejected by default to avoid silent downgrade. `allowUnauthenticated=true` can deliberately relax that behavior for mixed development environments.

The environment variables `ESPNS_AUTH_KEY`, `ESPNS_ALLOW_UNAUTHENTICATED`, and `ESPNS_CONFIG` can override file-based configuration.

Authenticated sessions now negotiate `mode=aes256-gcm`. The PSK and fresh client/server nonces feed HKDF-SHA256, which derives independent host→device and device→host AES-256-GCM keys plus nonce prefixes. Serial payload is encrypted and integrity-protected in framed records. Unauthenticated sessions remain `mode=raw` plaintext.

## Reconnect grace

A temporary ESP32 disconnect no longer immediately tears down the Arduino IDE monitor session.

The host keeps the IDE-side connection open and retries the ESP32 TCP connection for up to 15 seconds. This covers typical reset/reboot and short Wi-Fi interruption cases. If the board is still unavailable after the grace period, the normal `port_closed` event is emitted.

The direct `--connect` test mode uses the same reconnect behavior.

## Build on Windows

~~~powershell
.\build-windows.ps1
~~~

This runs `go test ./...` and produces `espnetworkserial-monitor.exe` in this directory.

## Direct transport test

~~~powershell
.\espnetworkserial-monitor.exe --connect 192.168.1.128
~~~

This bypasses Arduino IDE and bridges stdin/stdout directly to the ESP32 TCP transport. It is useful for separating transport bugs from IDE integration bugs.

## Binary stress test

With the `examples/StressEcho` firmware loaded on the target:

~~~powershell
.\espnetworkserial-monitor.exe --stress 192.168.1.128
~~~

Defaults are 1 MiB per cycle, 5 cycles and a 2-minute timeout per cycle. The command reports connection state plus 10% echo progress. Override them with:

~~~powershell
.\espnetworkserial-monitor.exe --stress 192.168.1.128 --stress-bytes 8388608 --stress-cycles 100 --stress-timeout 5m
~~~

For a very small diagnostic run:

~~~powershell
.\espnetworkserial-monitor.exe --stress 192.168.1.128 --stress-bytes 4096 --stress-cycles 1
~~~

The tool generates deterministic binary data, sends it through the full ESPNS transport, verifies the echoed bytes exactly, closes the connection, and repeats with a fresh ESPNS handshake/session.

## Arduino IDE development integration

See [Windows development setup](../docs/development-setup.md).

The Windows installer and Linux/macOS terminal setup write an ESPNetworkSerial-managed `pluggable_monitor.pattern.network` recipe to each detected ESP32 core `platform.local.txt`. They preserve unrelated content and refuse to overwrite another third-party network monitor recipe.

## CI

GitHub Actions runs Go tests and cross-builds standalone binaries for Windows amd64, Linux amd64/arm64, and macOS amd64/arm64.

## Current security status

Optional mutual HMAC-SHA256 authentication is implemented. Authenticated sessions use an AES-256-GCM record layer with directional session keys derived by HKDF-SHA256. Unauthenticated sessions remain plaintext `mode=raw`.
