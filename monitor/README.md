# ESPNetworkSerialMonitor

The host-side bridge used by Arduino IDE.

## Current prototype

The monitor is implemented in Go using only the standard library. It implements Arduino Pluggable Monitor protocol v1 commands:

- `HELLO`
- `DESCRIBE`
- `CONFIGURE`
- `OPEN`
- `CLOSE`
- `QUIT`

For `network` ports, `OPEN` connects to the selected ESP32 address on TCP port `3233` and bridges bytes bidirectionally to Arduino IDE's callback connection.

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

## Arduino IDE development integration

See [Windows development setup](../docs/development-setup.md).

The current installer writes a development-only `pluggable_monitor.pattern.network` recipe to `platform.local.txt`. This is intentionally not the final distribution mechanism.

## CI

GitHub Actions runs Go tests and cross-builds standalone binaries for Windows amd64, Linux amd64/arm64, and macOS amd64/arm64.

## Current security status

The transport is still pre-alpha plaintext TCP without authentication. Use it only on a trusted LAN until the protocol/security layer is implemented.
