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

The stress command fails immediately on connection/authentication failure, write/read failure, incomplete echo, or the first wrong echoed byte.

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
