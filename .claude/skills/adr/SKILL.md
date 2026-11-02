---
name: adr
description: Write a new ADR from the template.
argument-hint: <one-sentence decision> [context bullets]
---

New ADR: $ARGUMENTS

- Next number: the highest in `ls docs/adr/` + 1. Copy `docs/adr/0000-template.md` to
  `docs/adr/<NNNN>-<slug>.md`.
- Fill in context (2–3 bullets), the decision, the alternatives considered, and the consequences.
  Under 40 lines.
- If it carries over an archive ADR (`~/dev/sdr-vantage/docs/adr/`), add "was ADR-0xx" to
  **Related** and drop the parts about parked product work.
