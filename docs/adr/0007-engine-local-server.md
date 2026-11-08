# ADR-007: The engine is always a local server

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** docs/api/README.md; was ADR-005

## Context
The browser UI, a later Electron shell, and scripts should not need separate code paths into the
engine, and the engine must stay headless so it can be tested without a UI.

## Decision
The engine always runs as a server: loopback WebSocket + HTTP with a per-session token and
Origin/Host checks (docs/api/README.md). Commands and events are JSON; bulk streams are binary
frames (ADR-010). The schema is versioned, with generated TypeScript types.

## Consequences
Every client, our UI included, goes through the same checked API. Loopback security has to be
right from the first commit. Remote access stays off (ADR-009).
