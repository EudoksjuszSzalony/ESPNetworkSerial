# Security

> Status: design draft.

ESPNetworkSerial carries console data and may allow commands to be sent back to the ESP32, so authentication cannot be treated as an afterthought.

## Goals

- Do not hard-code credentials or project-specific secrets into the protocol.
- Make unauthenticated mode explicit when it is supported.
- Provide replay-resistant authenticated sessions.
- Keep authentication independent from application-specific systems such as UTFA.
- Document what is and is not encrypted.
- Fail clearly when the host and device disagree on security capabilities.

## Candidate authenticated mode

HMAC-SHA256 with a nonce/challenge is a candidate for the first authenticated mode. The exact handshake is intentionally not frozen yet.

## Threat model to document before v1

- Untrusted client on the same LAN.
- Replay of captured authentication messages.
- Accidental connection to the wrong ESP32.
- Credential leakage through logs or configuration files.
- Denial-of-service and reconnect loops.

Encryption and authentication are separate properties. If a transport is plaintext TCP, successful authentication alone does not make console contents confidential.
