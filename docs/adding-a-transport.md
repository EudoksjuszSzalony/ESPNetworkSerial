# Adding Another Transport

> Status: v0.1 extension boundary defined. TCP is the reference transport. The public sketch facade is the compatibility boundary; concrete transport registration APIs intentionally remain experimental.

ESPNetworkSerial should make transport extensions boring: implement a small contract, register it, test it, document it. Ordinary sketches should keep using the high-level `ESPNetworkSerial` facade rather than becoming coupled to a concrete backend.

## Expected responsibilities

A transport implementation should eventually provide:

- `Open` / connect
- `Close`
- byte-oriented `Read`
- byte-oriented `Write`
- capability reporting
- transport-specific configuration

Discovery should remain separate where possible so a transport can be used with manual addressing even when discovery is unavailable.

## Conceptual Go interface

~~~go
type Transport interface {
    Open(config Config) error
    Close() error
    Read([]byte) (int, error)
    Write([]byte) (int, error)
    Capabilities() Capabilities
}
~~~

This interface is illustrative and is **not** part of the v0.1 compatibility promise.

## Checklist for a new transport

1. Implement the transport contract.
2. Add configuration parsing.
3. Add optional discovery integration.
4. Define security behavior.
5. Add loopback and disconnect/reconnect tests.
6. Verify bidirectional Arduino Serial Monitor traffic.
7. Document platform limitations.

TCP is the current reference implementation. For v0.1, new transport work must stay behind the ordinary `ESPNetworkSerial` facade. A concrete multi-transport registration API is deliberately deferred until at least one second transport exists, avoiding a premature public abstraction.

## Firmware-side rule

The public sketch API should remain shaped like:

~~~cpp
ESPNetworkSerial MySerial;
MySerial.begin();
MySerial.handle();
MySerial.println(...);
~~~

Concrete backends such as `ESPNetworkSerialTCP` are implementation/advanced-use building blocks. New transports should not force ordinary sketches to rename every API call or manage backend objects manually.

The current facade exposes `tcp()` as an advanced escape hatch for TCP-specific work. It is explicitly outside the v0.1 source-compatibility promise. Future multi-transport registration may replace or extend that mechanism, so transport-specific APIs should stay out of the stable facade unless they make sense for every backend.

## v0.1 extension policy

For the v0.1 line:

- `ESPNetworkSerial` is the stable sketch-facing facade;
- ESPNS/1 wire compatibility is governed by `protocol.md`;
- `ESPNetworkSerialTCP`, `ESPNetworkSerialMux`, `tcp()`, and host-side transport interfaces are advanced/experimental;
- adding another transport must not require ordinary sketches to abandon the facade;
- incompatible changes to the stable facade or ESPNS/1 wire format are not allowed merely to make a new transport easier to implement.

This policy is the v0.1 extension-point stabilization: the boundary is frozen, while the internals behind that boundary are intentionally free to evolve.
