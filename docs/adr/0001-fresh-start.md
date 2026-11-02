# ADR-001: Fresh start as a public repository

- **Status:** Accepted
- **Date:** 2026-11-02
- **Related:** ADR-002; was ADR-036 in the private predecessor repo

## Context
- Bandlantern began in a private repository (internal name SDR Vantage) as a planned commercial
  product. That repo's history holds material that shouldn't be published, such as notes from a
  user-research round about third parties, alongside a large parked product plan.
- Its DSP blocks (DDC, filters, resampler, demodulators, AGC, and so on) were written by an AI
  assistant for speed. Since ADR-002 the point of the project is the owner's own understanding
  of DSP, so the history should show plainly who wrote what.

## Decision
- Start this repository, public from its first commit, under the MIT license. The predecessor
  stays private as an archive.
- Plumbing is ported from the archive **lazily**, only when the next curriculum module needs it:
  build and CI, the engine–UI protocol, the engine server, rtl_tcp access, file readers and
  writers, audio device output, the SPSC ring, controller wiring, the UI, and the desktop shell.
  It is AI-assisted and labeled as such.
- **All DSP is the owner's**: the spectrum path (module A0), the playback pipeline threads, the
  receive chain, every demodulator and decoder, and their Python prototypes and golden vectors.
  None of the archive's DSP code, DSP docs, or golden vectors is carried over. The engine
  exposes seams for DSP blocks; a feature reports "not available" until its block exists.
- ADRs restart at 001. Still-valid decisions come over renumbered, each with a "was ADR-0xx"
  line, when the code they govern arrives.

## Consequences
- Easier: a clean public history in which every DSP block is visibly the owner's.
- Harder: the bench shows nothing until A0 lands and plays nothing until A1 and a demodulator do.
  The archive's bench still runs for comparison by ear.
