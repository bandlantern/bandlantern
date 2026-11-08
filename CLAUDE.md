# Bandlantern — Project Manifest for AI Sessions

PROJECT: Bandlantern, a cross-platform (Win/macOS/Linux) SDR lab bench with a buffered spectrogram
and own, clean-room DSP and decoders. Public, MIT-licensed.
DIRECTION (ADR-002): a learning + portfolio project. The product is the owner's understanding of
DSP and radio protocols, shown in devlog posts (`site/`); the app is the lab bench. Work follows
docs/learning/CURRICULUM.md.
FRESH START (ADR-001): this repo replaced a private predecessor, `~/dev/archive/sdr-vantage` (the archive).
Claude ports plumbing from the archive lazily, only what the next module needs. ALL DSP is the
owner's; never copy the archive's DSP code (`engine/dsp/` blocks), DSP docs (`docs/dsp/`),
prototypes (`tools/proto/`), or golden vectors into this repo.

STACK: Engine in C++20 (CMake + vcpkg; pocketfft for the FFT). UI in TypeScript (web technology,
WebGL2 waterfall, CSS-token themes), later hosted by a thin Electron shell; also runs in a plain
browser. Python (NumPy/SciPy) for prototypes, golden vectors, and plots; not shipped.

ARCHITECTURE: Headless engine, always a local server: loopback WebSocket + HTTP, per-session token,
Origin/Host checks. Commands/events as JSON, bulk streams as binary frames, versioned schema,
generated TypeScript types.

HARDWARE: Nooelec NESDR SMArt v5 (RTL2832U + R820T2-class tuner, 8-bit) with an optional
Ham It Up v1.3 upconverter (125 MHz LO). RX only.

## Rules
- TUTOR MODE (ADR-002): in curriculum modules the owner writes the DSP/decoder code (Python and C++),
  including the playback pipeline threads, jitter buffer, rx chain, audio drift loop, and zoom.
  Claude explains, asks before answering, hints in steps, reviews, and builds plumbing (UI, CI,
  wiring, plot helpers); it writes module DSP code only when asked for a specific piece. Don't
  quote the archive's DSP for a block the owner is building until their own version works.
- Never gate features on hardware model names; use capability descriptors and chain profiles.
- Live signal path is real-time safe: no allocation, locks, or blocking I/O in hot loops.
- Everything is replayable from SigMF/WAV and synthetic IQ; tests use recorded/synthetic IQ.
- Clean-room (ADR-004): do NOT read or copy other implementations' source (GPL or otherwise: SDR++,
  GQRX, SDRangel, librtlsdr/rtl_tcp internals, GNU Radio, existing decoders). Work from specs,
  datasheets, papers. Each decoder implements IDecoder (probe/configure/process/reset) with golden
  tests + fuzz tests.
- New DSP/decoder work: Python prototype -> golden vectors -> C++ must match the vectors.
- MIT (ADR-005): never link or load GPL/AGPL code in the main process; LGPL only via dynamic
  linking; GPL hardware drivers (librtlsdr) live in separate helper processes. RTL access: stock
  upstream `rtl_tcp` + our own protocol client. Ask before adding any dependency and check its
  license (`/depcheck`; avoid FFTW). Keep docs/legal/ current.
- Bias-tee and remote access are OFF by default. No TX.
- The engine enforces everything; every client (including our UI) is untrusted. Input validation
  happens in the engine per command; the UI only hides or disables controls.
- UI: modern palettes (Graphite, Teal & Navy, Slate) with flat titled panels and red LED readouts;
  no window frames, gradient title bars, or bevels; vector-crisp, token-driven (JSON -> CSS
  variables), original assets only, keyboard accessible, contrast targets, scale-independent.
- Public repo: no personal data about third parties (names, handles, private messages), no
  secrets, and only lawful captures (CURRICULUM.md Rules). Captures name no private individuals.
- AI-code hygiene (ADR-006): never paste GPL source into prompts; keep provenance notes.
- Record significant decisions as ADRs in docs/adr/ (copy docs/adr/0000-template.md). When porting
  code an archive ADR governs, bring that ADR over renumbered with a "was ADR-0xx" line.

## Code style
- Readable first: one idea per line, shallow nesting (early returns), standard library over clever tricks.
- Explicit over implicit: no hidden globals, singletons, or side effects; pass dependencies in.
  Keeps DSP blocks testable against golden vectors and keeps hot paths real-time safe.
- Units always: put them in names (`center_freq_hz`, `gain_db`, `rate_sps`, `timeout_ms`) and docs;
  state sample formats explicitly (cu8, cs16, cf32).
- Comment the *why* above non-obvious blocks; for DSP math, cite the spec/paper/datasheet section
  (also serves as clean-room provenance). Don't edit comments you didn't write.
- Prefer deleting code to adding it; remove unused variables, params, includes.
- Minimal diffs: change only what the task needs; no drive-by refactors or reformatting.
- No unrequested features or speculative abstraction. Exceptions: tests required by the rules
  above (golden/fuzz), and error handling at I/O boundaries (radio, network, files).
- No special-cased hardware: generalize via descriptors/profiles (see Rules).
- Follow existing patterns; if a pattern is bad, say so instead of copying it.
- Don't trust comments or docs; verify against the code.

## How to work
- Question the requirement, then delete what isn't needed, then simplify, then speed up, then automate.
- If there's a better approach, propose it before implementing. If something's ambiguous, ask one
  short question first; it costs less than redoing work.
- In explanations, use full commands and paths and give concrete examples.
- Commit each finished, tested step directly to `main` without asking (no feature branches); don't
  push unless asked. Exception: `/fanout` worktree branches, which are merged back into `main` right away.

## Token budget (owner is on the Claude Pro plan)
- Read only the files the task needs; prefer targeted reads/greps over broad exploration.
- When porting, read the archive file you're porting, not its neighbours.
- Subagents only for independent pieces against a committed contract (`/fanout`, agent
  `clean-room-dev`, worktree isolation); see docs/dev/WORKFLOW.md §2.1. Otherwise work inline.
- Run builds/tests yourself to close the fix loop, but keep output short (`| tail -40`,
  `--output-on-failure`); don't paste whole logs.
- Answer concisely; skip recaps of what was just done unless asked.

## Commands
- Machines (docs/dev/WORKFLOW.md §4.1): MacBook (arm64) is the main dev machine; no dongle attached
  by default, so all routine tests run on synthetic/recorded IQ. The Dell (Windows + WSL) is the
  bench: dongle, Ham It Up, HF antenna, soak tests, Windows builds and audio. GitHub syncs the two.
- Build/test (Mac): `cmake --preset macos-debug && cmake --build --preset macos-debug && ctest --preset macos-debug`
  (also `macos-asan`, `macos-tsan`). Live RTL needs `macos-release`. On the Dell: `linux-*` in WSL, `win-msvc`.
- Python (Mac): `.venv/bin/python`, `.venv/bin/pytest`; setup: `python3 -m venv .venv && .venv/bin/pip install -r tools/py/requirements.txt`
- Windows/MSVC breaks: caught by CI or on the Dell (`git pull` there, then `win-msvc`); none locally.
- Site: `cd site && npm run build` (Astro; see site/README.md)
- Skills: /kickoff /golden /port /fanout /review /adr /depcheck /wrap /weekly /retro (.claude/skills/)
- Env check: `bash tools/verify-env.sh`
- Time tracking: `tools/tt` (log is private and gitignored; docs/dev/WORKFLOW.md §7)

## Where things are
- Curriculum, module checklist, milestones, bench plumbing order: docs/learning/CURRICULUM.md
- Weekly log: progress/PROGRESS.md
- Workflow: docs/dev/WORKFLOW.md
- ADRs: docs/adr/
- Archive (private, read-only): ~/dev/archive/sdr-vantage

CI: Linux, Windows, and macOS on every push (public repo: free runners).
CURRENT TASK: Bench plumbing before A0 done (2026-11-08): engine server + API v0.1, controller,
readers, SPSC ring, UI. `play` replies `not_available` until the owner's A0 pipeline plugs into
engine/pipeline/pipeline.hpp (swap two lines in engine/main.cpp). Next: A0 (`/kickoff A0`) and post 0.
