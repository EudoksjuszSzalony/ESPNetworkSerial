# Testing and Hardening

> Status: evolving pre-release test plan.

ESPNetworkSerial has two separate test layers:

1. deterministic host-side protocol/crypto tests that run in CI;
2. real-device validation on ESP32 hardware through Arduino IDE.

## Automated host-side coverage

The Go monitor test suite currently covers:

- HKDF-SHA256 against RFC 5869 test vectors;
- mutual ESPNS HMAC handshake;
- authenticated-mode downgrade rejection;
- AES-256-GCM round trips;
- payload sizes around record boundaries;
- multi-record payloads;
- 1 MiB simultaneous bidirectional encrypted streams;
- ciphertext tampering;
- authenticated-header tampering;
- replayed records;
- out-of-order records;
- zero-length and oversized records;
- truncated records;
- TX/RX sequence exhaustion;
- reconnect behavior;
- host builds for Windows, Linux and macOS.

The CI test job also runs the Go race detector.

## Secure-record boundary matrix

The automated suite exercises plaintext payload sizes including:

~~~text
1
2
15
16
17
255
256
1023
1024
1025
4096 bytes
~~~

The 1024-byte size is the current maximum ESPNS plaintext record size. Larger writes must be split into multiple authenticated records without changing the bytes visible through the Stream API.

## Failure behavior

Once an authenticated ESPNS session enters `mode=aes256-gcm`, invalid record state is fail-closed.

The connection must not expose plaintext after any of these conditions:

- invalid GCM tag;
- modified authenticated header;
- repeated sequence number;
- skipped/out-of-order sequence number;
- impossible record length;
- truncated record;
- exhausted sequence space.

The connection is discarded rather than attempting to resynchronize inside the encrypted byte stream.

## Hardware validation checklist

Before a protocol release candidate, validate on a real ESP32:

- Serial Monitor opens over Wi-Fi with the correct PSK;
- wrong PSK is rejected;
- restoring the correct PSK reconnects;
- USB Serial and Wi-Fi Serial can coexist;
- ASCII TX/RX works in both directions;
- arbitrary/binary payload echo works;
- OTA succeeds while the network monitor exists;
- Serial Monitor reconnects after the OTA reboot;
- repeated manual reset reconnects;
- Wi-Fi interruption/recovery reconnects within the grace period;
- authenticated traffic reports `mode=aes256-gcm`;
- unauthenticated development mode still reports `mode=raw`.

## Real-device binary stress harness

The repository includes `examples/StressEcho`, a minimal byte-for-byte echo firmware intended specifically for transport testing.

Copy its local credentials:

~~~text
examples/StressEcho/secrets.example.h
-> examples/StressEcho/secrets.h
~~~

Use the same Wi-Fi credentials and ESPNS authentication key as the host monitor, then upload `StressEcho` to the ESP32.

Build the host monitor and run:

~~~powershell
.\monitor\espnetworkserial-monitor.exe --stress 192.168.1.128
~~~

Default stress parameters:

~~~text
1 MiB deterministic binary payload per cycle
5 connect/authenticate/echo/disconnect cycles
2 minute timeout per cycle
exact byte-for-byte verification
10% progress reports
~~~

For a quick diagnostic run:

~~~powershell
.\monitor\espnetworkserial-monitor.exe --stress 192.168.1.128 --stress-bytes 4096 --stress-cycles 1
~~~

Longer runs can be requested explicitly:

~~~powershell
.\monitor\espnetworkserial-monitor.exe --stress 192.168.1.128 --stress-bytes 8388608 --stress-cycles 100 --stress-timeout 5m
~~~

Every cycle creates a new TCP/ESPNS session, so authenticated mode also exercises fresh handshake nonces, HKDF session-key derivation and AES-GCM sequence state repeatedly.

### Real-device baseline

An Adafruit Feather ESP32 V2 running authenticated `StressEcho` completed the default 5 × 1 MiB profile with exact byte-for-byte verification at about 2.45 Mbit/s aggregate. This is a development measurement from one device/network setup, not a guaranteed throughput figure.

The bulk-read firmware path consumes a verified plaintext record in one buffer copy instead of repeatedly polling the network for each byte.

Additional real-device torture runs completed successfully:

- one authenticated 64 MiB echo cycle (128 MiB aggregate application traffic) at about 1.84 Mbit/s aggregate;
- 100 consecutive authenticated 64 KiB echo sessions (12.50 MiB aggregate application traffic) at about 1.62 Mbit/s aggregate.

These runs exercise two different failure surfaces: long-lived encrypted record sequencing and repeated TCP/HMAC/HKDF/AES-GCM session lifecycle. They are development observations, not guaranteed performance figures.

By default, the stress command fails immediately on connection/authentication failure, write/read failure, incomplete echo, or the first wrong echoed byte.

### Manual fault-injection / recovery mode

`--stress-recover` changes only the test harness behavior. If an active transfer is broken, the host waits for the ESP32 to become reachable again, requires a completely fresh ESPNS handshake, then restarts the interrupted logical cycle from byte zero. It never resumes an AES-GCM byte stream across sessions.

~~~powershell
.\\monitor\\espnetworkserial-monitor.exe --stress 192.168.1.128 --stress-bytes 8388608 --stress-cycles 1 --stress-timeout 2m --stress-recover --stress-recover-timeout 45s
~~~

While this is running, reset the ESP32 or temporarily remove its Wi-Fi connectivity after progress has started. A successful test must report an interruption, a successful fresh handshake, restart the cycle, verify the entire deterministic payload, and finish with at least one recovery.

This mode deliberately treats bytes from the interrupted attempt as uncommitted. That is the safe session boundary for the harness: new handshake nonces, HKDF keys, nonce prefixes and AES-GCM sequence numbers belong to a new stream.

Real-device reset fault injection has been verified: an authenticated transfer was interrupted by a physical ESP32 reset, the host detected the broken session, a fresh authenticated ESPNS session was established after reboot, and the logical cycle restarted and completed with byte-for-byte verification.

#### Wi-Fi-loss fault injection

The same recovery mode can test a live ESP32 whose network path disappears without resetting the MCU. Start the 8 MiB recovery profile above, then temporarily disable the board's Wi-Fi path after transfer progress begins (for example by disabling the AP/SSID used by the board or otherwise isolating that client), keep it unavailable for several seconds, then restore it before `--stress-recover-timeout` expires.

Expected behavior is the same at the protocol boundary: the interrupted encrypted session is discarded, recovery requires a new authenticated handshake, and the logical payload restarts from byte zero. Unlike the reset test, this scenario exercises TCP/Wi-Fi loss while the application and MCU remain alive.

## Longer-running hardware torture test

Before freezing ESPNS v1, run a dedicated hardware soak test rather than relying only on Arduino Serial Monitor output.

Target scenarios:

- many megabytes of deterministic binary traffic in each direction;
- hundreds or thousands of connect/authenticate/disconnect cycles;
- repeated ESP32 resets while the host monitor remains open;
- temporary Wi-Fi loss during active traffic;
- abrupt TCP loss during a multi-record write;
- long-running traffic while ArduinoOTA remains available.

The soak test should validate byte-for-byte payload hashes/counters rather than visual terminal output.

## What CI does not prove

Passing these tests does not constitute a cryptographic audit.

CI can detect implementation regressions, framing errors, concurrency bugs and known failure-path mistakes. It cannot establish that the overall protocol design is suitable for every production threat model.
