# ADR-009: The engine enforces everything; every client is untrusted

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-007; was ADR-018 (trimmed: the archive's licensing and external-API parts
  don't apply to an MIT learning project)

## Context
The API is readable JSON over `ws://` on loopback. Anyone can read it in DevTools or the UI
bundle and write a client that sends any message.

## Decision
1. Keep the protocol readable: JSON for control, binary frames for bulk. No obfuscation.
2. The engine validates every command itself and applies the safety interlocks (bias-tee off
   by default, no TX). Refusals are error replies. The UI only hides or disables controls.
3. Loopback may use plain `ws://`. Remote access stays off; turning it on later needs TLS and
   its own ADR.
4. The protocol is internal and may change while the API is `0.x`.

## Consequences
- Each new command gets an engine-side check and a test that a bad value is refused.
- Revisit before any remote access or before publishing the API for third-party clients.
