---
name: kickoff
description: Start a work slice (a module step or a plumbing stage). Short plan, no code yet.
argument-hint: <module e.g. A0, or plumbing stage> <one concrete deliverable>
---

Kickoff for: $ARGUMENTS

1. Read only the matching module or plumbing stage in `docs/learning/CURRICULUM.md` and the
   matching `now` issue if there is one (`gh issue list -l now`).
2. Module work (tutor mode): name the module-loop step, the concepts and references to read, and
   2–3 questions for me to answer before any code. Don't plan the DSP implementation for me.
3. Plumbing: list the archive files to port (`~/dev/archive/sdr-vantage/...`; never DSP blocks, DSP
   docs, prototypes, or golden vectors), what changes on the way (names, seams for my blocks),
   the archive ADRs to bring over, the tests that define "done", and the exact build/test command.
   No code yet.
4. Say whether the slice should be done in this session or fanned out (`/fanout`, see
   docs/dev/WORKFLOW.md §2.1).
5. If a decision is ambiguous, ask one short question instead of guessing.
