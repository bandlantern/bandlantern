---
name: golden
description: Review the owner's Python prototype and golden vectors for a module block (tutor mode; no DSP code).
argument-hint: <block name>
---

Review my prototype and golden vectors for: $ARGUMENTS

Tutor mode (CLAUDE.md): don't write or rewrite the DSP. Point at problems and ask questions.

Check, most important first, at most 6 findings with `file:line`:
- The test signal is synthetic with a fixed seed, and the output is checked against an
  independent reference (a SciPy function, a spec example, or a closed-form result), with a stated
  tolerance and a reason for it.
- Vectors in `testdata/golden/<name>/`: explicit sample format (cf32, f32, cu8) and rate in sps,
  plus a `README.md` describing the format.
- A pytest under `tools/py` regenerates the vectors and checks them against the reference.
- A provenance note in `docs/dsp/<name>.md` (or `docs/decoders/<mode>/`): spec sections and design
  decisions, in my words.
- Numerical issues you can see (scaling, off-by-one in filter delay, float precision).

Run `.venv/bin/python -m pytest -q tools/py` and report the result. Fix only plumbing (paths,
test wiring) yourself, and say so.
