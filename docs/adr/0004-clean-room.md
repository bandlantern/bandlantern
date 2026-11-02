# ADR-004: Own DSP and decoders, clean-room

- **Status:** Accepted
- **Date:** 2026-10-25 (amended 2026-11-02 for the MIT license and ADR-002)
- **Related:** ADR-002, ADR-005; was ADR-003

## Context
Writing the DSP and decoders is the point of the project (ADR-002), and copying an existing
implementation would defeat it. GPL code also can't enter an MIT-licensed program (ADR-005).

## Decision
Write all DSP and decoders ourselves from published specifications, papers, datasheets, and
books. Never read or copy other implementations' source (GPL or otherwise) for a block before
our own works: SDR++, GQRX, SDRangel, librtlsdr/`rtl_tcp` internals, GNU Radio, existing decoders.
Afterwards, compare outputs and published numbers, not code. Keep provenance notes per block:
the spec sections and the design decisions.

## Consequences
Large effort, especially for weak-signal sensitivity. Python prototypes and the devlog posts
double as provenance records.
