# ADR-005: No GPL/AGPL in the main process

- **Status:** Accepted
- **Date:** 2026-10-25 (amended 2026-11-02 for the MIT license)
- **Related:** docs/legal/dependencies.md; was ADR-010

## Context
The code is MIT-licensed (ADR-002). Linking or loading GPL/AGPL code into the main process would
make the combined program GPL.

## Decision
Nothing GPL/AGPL is linked into or loaded by the main process. LGPL only via dynamic linking with
notices and replaceability. GPL drivers (librtlsdr) run as separate helper executables over a
local socket. Stage 1: stock, user-installed upstream `rtl_tcp` with our own clean-room protocol
client. A custom helper, if needed later, lives in its own public repository, never here.

## Consequences
Every new dependency is license-checked before adoption (`/depcheck`), and a license scan with
an SBOM runs in CI once the engine has dependencies beyond the test framework.
