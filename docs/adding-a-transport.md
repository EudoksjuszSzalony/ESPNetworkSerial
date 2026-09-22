# Adding Another Transport

> Status: architecture target. Concrete interfaces will be updated when the host monitor implementation lands.

ESPNetworkSerial should make transport extensions boring: implement a small contract, register it, test it, document it.

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
