# Adding Another Transport

> Status: evolving architecture. TCP is the current reference transport; extension points are not frozen.

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

This interface is illustrative, not yet API-stable.

## Checklist for a new transport

1. Implement the transport contract.
2. Add configuration parsing.
3. Add optional discovery integration.
4. Define security behavior.
5. Add loopback and disconnect/reconnect tests.
6. Verify bidirectional Arduino Serial Monitor traffic.
7. Document platform limitations.

The first reference implementation will be TCP and will define the concrete extension points used by subsequent transports.

## Firmware-side rule

The public sketch API should remain shaped like:

~~~cpp
ESPNetworkSerial MySerial;
MySerial.begin();
MySerial.handle();
MySerial.println(...);
~~~

Concrete backends such as `ESPNetworkSerialTCP` are implementation/advanced-use building blocks. New transports should not force ordinary sketches to rename every API call or manage backend objects manually.

The current facade exposes `tcp()` as an escape hatch for TCP-specific work. Future multi-transport registration may replace or extend that mechanism, so transport-specific APIs should stay out of the common facade unless they make sense for every backend.
