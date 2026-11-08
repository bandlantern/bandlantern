# ADR-010: Engine API encoding: JSON commands and events, binary stream frames

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-007, ADR-008; contract in docs/api/; was ADR-024

## Context
A solo developer wants a small toolchain and messages that are readable while debugging. The
archive measured this encoding end to end (parse plus texture upload about 0.05 ms per row,
row queued to drawn p50 6.4 ms, engine CPU 7 % at 2.4 MS/s).

## Decision
JSON text messages for commands, replies and events; versioned binary frames (fixed
little-endian header plus typed payload) for spectrum rows and later bulk streams, both on the
one authenticated WebSocket. No protobuf, CBOR or MessagePack. The JSON shapes live in
`docs/api/schema/api.schema.json`; `tools/py/api_schema.py` generates `ui/src/api.generated.ts`.

## Alternatives considered
- **Protobuf / FlatBuffers:** codegen for every message, a C++ dependency, and opaque frames in
  DevTools, with no measured need.
- **Everything JSON:** spectrum rows as JSON arrays are many times larger and slower to parse.

## Consequences
- Commands stay readable in DevTools (ADR-009).
- A new stream kind gets a frame `kind`, a payload format in frames.md, and golden tests on
  both sides.
