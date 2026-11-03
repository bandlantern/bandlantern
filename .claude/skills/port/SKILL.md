---
name: port
description: Review the owner's C++ port of a block against their golden vectors and the real-time rules (tutor mode; no DSP code).
argument-hint: <block name>
---

Review my C++ port of: $ARGUMENTS

Tutor mode (CLAUDE.md): don't write or rewrite the DSP. Point at problems and ask questions.

- Build and test with trimmed output:
  `cmake --build --preset macos-debug 2>&1 | tail -20 && ctest --preset macos-debug --output-on-failure | tail -40`.
  Once it passes, run `macos-asan` too.
- Check, at most 6 findings with `file:line`: the Catch2 test loads my vectors and compares within
  the README's tolerance; no allocation, locks, or I/O in `process()` (allocate in
  configure/reset); spec citations in comments match the prototype's; units in names; sample
  formats stated.
- You may wire the block into the engine (CMake, controller seams) yourself; that's plumbing.
