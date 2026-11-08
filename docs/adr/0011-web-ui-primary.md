# ADR-011: Web-technology UI, served by the engine

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-007, ADR-009; was ADR-002

## Context
A solo developer cannot afford two UI codebases. The UI has to run in a plain browser now and in
a thin desktop shell later, against the same engine API.

## Decision
One TypeScript web UI (WebGL2 waterfall, CSS-token theming) served by the engine (`GET /` serves
`ui/dist`) and later hosted by a thin Electron shell. Qt 6 Widgets is the documented fallback if
the waterfall cannot hold about 30 fps in a plain browser, or the look needs heroic effort.

## Consequences
The UI stays thin: no DSP, protocol, or decoder logic in TypeScript. It is a client like any
other (ADR-009), so the engine checks every command. Shipped UI code is inspectable, so nothing
sensitive lives there.
