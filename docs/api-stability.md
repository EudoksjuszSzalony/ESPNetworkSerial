# Firmware API Stability

> Status: v0.1 freeze candidate.

This document separates the ordinary sketch API from advanced implementation and diagnostic surfaces.

## v0.1 facade stability candidate

Ordinary sketches should use either the global `ESPSerial` object or their own `ESPNetworkSerial` instance.

The facade surface intended to remain source-compatible throughout the v0.1 line includes:

- lifecycle: `begin()`, `end()`, `handle()`;
- companion streams: `addStream()`, `removeStream()`, `clearStreams()`, `streamCount()`;
- authentication: `setAuthKey()`, `clearAuthKey()`, `authenticationEnabled()`;
- connection waits: both `waitForConnection()` overloads;
- state: `started()`, `connected()`, `port()`, `remoteIP()`;
- Arduino `Stream`/`Print` operations: `write()`, `available()`, `read()`, bulk `read(buffer,size)`, `peek()`, and `flush()`.

The global convenience instance remains:

~~~cpp
extern ESPNetworkSerial ESPSerial;
~~~

## Behavioral commitments

- `begin()` starts the built-in network serial listener.
- `end()` stops the listener and active ESPNS client.
- `handle()` services connection, handshake, and receive state and is safe to call frequently from `loop()`.
- writes are fanned out to the network transport and attached companion streams;
- reads are selected fairly across available inputs;
- `clearStreams()` removes companions but keeps the built-in network transport;
- changing authentication state closes any active ESPNS client;
- every TCP reconnect creates a new ESPNS session;
- no-argument `waitForConnection()` deliberately blocks; the timeout overload bounds the wait.

## Advanced / experimental surfaces

These remain available but are **not** part of the v0.1 facade-stability promise:

- `ESPNetworkSerialTCP`;
- `ESPNetworkSerialMux`;
- `ESPNetworkSerial::tcp()`;
- `ESPNetworkSerialTCP::disconnectClient()`;
- internal sizing/timing macros other than the documented default port;
- concrete future transport-extension mechanisms.

The mux's special bulk-read fast path is private implementation plumbing between the facade and its known TCP backend.

## Versioning intent

Before v1.0, incompatible changes to advanced APIs remain possible. The project should avoid breaking the ordinary facade after v0.1.0 unless correctness or security requires it.

Wire compatibility is governed separately by [protocol.md](protocol.md). A library release number does not itself imply an ESPNS wire-protocol change.
