---
name: review
description: Quick pre-commit review of git diff against the project rules.
---

Review `git diff` (and `git diff --cached`) for, most severe first:
1. Real-time-safety violations in the live signal path (allocation, locks, blocking I/O).
2. License/clean-room issues: new dependencies, code that looks copied, missing spec citations on
   DSP math, archive DSP code or golden vectors brought into this repo.
3. Missing tests (golden/synthetic IQ for DSP; tests for new API messages).
4. Obvious bugs, missing units in names, unstated sample formats.
5. Secrets, machine-specific paths, or data about third parties (public repo).

Findings only, at most 5, each with `file:line`. No rewrites, no summary of the diff.
