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

## Build on Windows

```powershell
.\build-windows.ps1
```

This runs `go test ./...` and produces `espnetworkserial-monitor.exe` in this directory.

## Direct transport test

```powershell
.\espnetworkserial-monitor.exe --connect 192.168.1.128
```

This bypasses Arduino IDE and bridges stdin/stdout directly to the ESP32 TCP transport. It is useful for separating transport bugs from IDE integration bugs.

## Arduino IDE development integration

See [Windows development setup](../docs/development-setup.md).

The current installer writes a development-only `pluggable_monitor.pattern.network` recipe to `platform.local.txt`. This is intentionally not the final distribution mechanism.

## CI

GitHub Actions runs Go tests and cross-builds standalone binaries for Windows amd64, Linux amd64/arm64, and macOS amd64/arm64.

## Current security status

The transport is still pre-alpha plaintext TCP without authentication. Use it only on a trusted LAN until the protocol/security layer is implemented.
