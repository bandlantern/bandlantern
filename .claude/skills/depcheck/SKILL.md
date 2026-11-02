---
name: depcheck
description: License and linking check for a candidate dependency under ADR-005.
argument-hint: <library> <purpose>
---

Dependency check: $ARGUMENTS

- Find the license from the project's own LICENSE file or official site (not its source code).
  Note the exact version you checked.
- Under ADR-005 (the code is MIT): no GPL/AGPL in-process, LGPL only via dynamic linking, GPL
  drivers only in separate helper processes. Avoid FFTW.
- Answer yes/no, how it would be linked (static/dynamic/helper process), and the notice obligations.
- If yes, add a row to `docs/legal/dependencies.md` in the existing format. Don't add it to
  `vcpkg.json` or `package.json`; I decide that.
