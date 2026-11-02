# ADR-006: AI-assisted development with provenance hygiene

- **Status:** Accepted
- **Date:** 2026-10-25 (amended 2026-11-02 for ADR-002)
- **Related:** ADR-002; was ADR-012

## Context
A solo half-time schedule benefits from AI assistance, but AI output can resemble licensed code
and raises ownership questions. Since ADR-002, AI-written DSP would also undercut the point of
the project.

## Decision
Claude (Claude Code) handles plumbing: scaffolding, UI, protocol, build and CI, tests for its own
code, docs, and reviews. In curriculum modules it tutors and reviews instead of writing the DSP
(ADR-002). Prompt from specs, never paste GPL source, keep provenance notes, and record human
design decisions. Commits made with Claude carry a `Co-Authored-By` trailer, so the history shows
which code is AI-assisted.

## Consequences
CLAUDE.md is the working agreement and is kept current. Each post ends with an AI-use note.
