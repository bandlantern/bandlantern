# Daily Workflow and Prompt Playbook

How a working day on Bandlantern should go: about 20 h/week, solo, using Claude Code on the **Pro plan**. The work itself follows `docs/learning/CURRICULUM.md` (ADR-002): Claude tutors in modules and builds plumbing.

---

## 1. Working within the Pro plan

Pro usage is limited by a **rolling ~5-hour window** plus a **weekly cap**. Exact numbers change, so check `/usage` (or `/status`) inside Claude Code rather than trusting this doc. What uses the budget is **how much context each message carries**, not the number of messages. A long conversation re-sends its whole history every turn.

**Rules that keep you inside the limits**

_Revised 2026-10-26:_ the first full day (~3–4 h, mostly docs/admin) used ~5% of the weekly cap. That projects to ~25–35% for a 20 h week, so there's headroom, roughly 2×, but code-heavy weeks and fan-outs use more. Rules 6, 8 and 9 now spend some tokens to save your hours. Check `/usage` at every `/wrap` during the first coding week. If the projection passes ~80%, tighten again (the old rules are in git history).

1. **One work session = one usage window.** Four sessions of about 5 h each spread across the week matches both the 20 h target and the weekly cap. Don't pile two big Claude sessions into one day.
2. **`/clear` between slices.** Each new task starts fresh, and `CLAUDE.md` reloads the context it needs. This is the biggest single saving.
3. **`/compact` only when you must keep going** inside one slice and the conversation is getting long. Check `/context` if you're unsure.
4. **Point at files, don't let Claude go looking for them.** Write "edit `engine/api/server.cpp`" or use `@engine/api/server.cpp` instead of "find where the server is".
5. **When porting from the archive, name the archive files** (`~/dev/sdr-vantage/...`); don't let Claude browse it.
6. **Let Claude run builds and tests** with trimmed output (`ctest --preset macos-debug --output-on-failure | tail -40`) so it closes the fix loop itself. Only soak tests and benchmarks go in your own terminal.
7. **Use plan mode (Shift+Tab) for anything larger than one file.** A cheap plan you approve beats an expensive wrong implementation you have to undo.
8. **Opus at medium effort for planning and hard work; Sonnet for implementing an approved plan and for delegated agents.** High effort for DSP math, real-time and threading work. See §2.
9. **Use subagents for independent parallel work** against a fixed interface, or for background work while you're on hardware (§2.1). Not for single-file tasks: each agent starts cold and re-reads context.
10. **When you hit the limit, switch to Claude-free work** (see the list in §5). That's not lost time; it's the other half of the job.

---

## 2. Model, effort, and subagents

**Default: Opus, medium effort** for kickoff/planning, DSP, protocol and debugging work. Implementing an approved plan or porting against golden vectors can drop to Sonnet if the usage check says so (below). Subagents (`clean-room-dev`) run on Sonnet. Raise to **high** for new DSP or decoder prototypes, real-time-safety or threading bugs, sync/timing loops, protocol framing, and any math you can't check by eye: wrong golden vectors poison every later test. Use **max** only for one stuck question, in a fresh session with only the files that matter.

**Escalation rule**
1. Two failed attempts at the same effort → raise effort one notch.
2. Before escalating, check the prompt: failing output pasted? file named? "done" defined? Missing context is the usual cause.
3. If high effort still fails, stop prompting: shrink the slice in plan mode, or write the Python prototype by hand (§5).

**Switch at `/clear` boundaries.** Changing model mid-conversation drops the prompt cache. If you must switch, `/clear` first and restate files, plan, and failing output.

**Budget check.** If `/usage` shows ~70% of the window or week used with real work left, implement on Sonnet/medium and save Opus for planning and DSP.

### 2.1 Subagents

The project agent `clean-room-dev` (`.claude/agents/clean-room-dev.md`) has the clean-room, real-time, and code-style rules built in and no web tools, so it can't end up reading GPL source. Use it for every delegated code task.

**Pattern: contract first, then fan out, then merge.**
1. In the main session, write the interface the pieces share (e.g. `docs/api/` message types, a SigMF layout, an `IDecoder` header). Commit it.
2. `/fanout <contract file> <piece 1>; <piece 2>; …` launches one `clean-room-dev` agent per piece, each in its own git worktree, in the background.
3. While they run, do hardware or Claude-free work (§5).
4. Merge the branches, build, run the tests, `/review`, commit.

| Good fit | Bad fit |
|---|---|
| Pieces that only share a written contract (engine ↔ browser ↔ Python generator) | Pieces that edit the same files, or an interface that isn't decided yet |
| Background UI work while you're on the dongle | Hardware debugging (needs you and the radio) |
| Test/fuzz harnesses for an existing interface; independent decoders | A one-file fix; anything sequential (prototype → vectors → port) |

## 3. Weekly shape (≈20 h)

Map these four session types onto whichever days suit you: two build sessions, one test/measure session, one admin/docs slot, and one demoable increment per fortnight.

| Session | Length | Focus | Claude use |
|---|---|---|---|
| **A: Module** | ~5 h | The current module: study, derivation, your Python and C++ | Tutor and review |
| **B: Module or plumbing** | ~5 h | Continue the module, or Claude ports the plumbing the next module needs | Heavy for plumbing |
| **C: Bench day (Dell)** | ~5 h | **Hardware smoke test with the dongle**, IQ recording for the Mac's replay tests, soak tests, Windows build and audio, perf measurements (§4.1) | Light (debug only) |
| **D: Write-up and reading** | ~5 h | The devlog post, ADRs, weekly review, spec reading | Light (edits only when asked) |

Every other week, end session D with something demoable: a screenshot, a short recording, or a passing benchmark.

---

## 4. A single session

```
START  (5 min)
  tools/tt start build "A0: window comparison"
  git pull
  open docs/learning/CURRICULUM.md → the current module (or the next plumbing stage)
  claude                     # fresh session in ~/dev/bandlantern, then /kickoff

WORK   (repeat per slice, 45–90 min each)
  /kickoff → approve plan → implement (Claude runs build/tests and fixes) → /review
  or: contract → /fanout → merge → /review
  commit when green
  /clear before the next slice

BREAKS
  tools/tt stop  …  tools/tt start build   (only count focused time)

WRAP   (10–15 min)
  tools/tt stop "what got done"   # the log is private (gitignored)
  /wrap   (ticks CURRICULUM.md, closes the issue, updates CURRENT TASK, commits)
  git push                        # GitHub is your backup and the bridge to the Windows clone
```

### 4.1 Two machines: Mac for development, Dell for the bench

- **MacBook (main, mobile):** all module and plumbing work, builds and tests (`macos-*` presets), Python (`.venv`), site. No dongle by default, so every routine test runs on **synthetic IQ** (fixed seed) or **recorded IQ** (SigMF/WAV). If a feature can only be checked with live hardware, add a replay path first.
- **Dell (stationary bench):** SMArt v5 + Ham It Up + HF antenna. Soak tests, live smoke tests, IQ recording, Windows build (`win-msvc`) and Windows audio, plus `linux-*` and `linux-tsan` in WSL.
- **Sync through GitHub:** `git push` on the Mac, `git pull --ff-only` on the Dell (Windows clone and WSL clone). Commit from one machine at a time.
- **Recordings:** capture on the Dell into `testdata/recordings/` (gitignored), with the SigMF metadata, then copy them to the same path on the Mac (`scp`/`rsync`, or a USB stick). Lawful captures only (CURRICULUM.md Rules).
- **Live IQ on the Mac without moving the dongle:** on the Dell run `rtl_tcp.exe -a <dell-lan-ip>`, then point the Mac engine at `<dell-lan-ip>:1234`. `rtl_tcp` has no authentication: home LAN only, start it for the session and stop it afterwards. To use the dongle directly, plug it into the Mac and run `rtl_tcp -a 127.0.0.1` (Homebrew).
- **WSL on the Dell** uses mirrored networking (`networkingMode=mirrored` in `.wslconfig`), so `rtl_tcp.exe -a 127.0.0.1` on Windows is reachable from the Linux engine at `127.0.0.1:1234`.

---

## 5. Claude-free work (for when the window runs out)

- Hardware: recording IQ, antenna and Ham It Up experiments, documenting results in `docs/hardware/`
- Reading specs (AX.25, PSK31, RTTY, FT8 protocol papers), with notes in `docs/decoders/<mode>/`
- Module work: derivations on paper, Python prototypes, measurements; this is most of the job
- Running and timing benchmarks; app fps measurements
- Devlog drafting, issue tracker grooming

---

## 6. Skills and prompts

The old prompt library is now project skills in `.claude/skills/`. Type the slash command with arguments:

| Command | Use it for | Effort |
|---|---|---|
| `/kickoff <module or stage> <slice>` | Start a slice: short plan, no code yet | medium |
| `/golden <block>` | Review your Python prototype and golden vectors against the module loop | high |
| `/port <name>` | Review your C++ port against your vectors and the real-time rules | medium (high for real-time issues) |
| `/fanout <contract> <piece>; <piece>; …` | Parallel `clean-room-dev` agents in worktrees (§2.1) | medium |
| `/review` | Check `git diff` before committing; at most 5 findings | medium |
| `/adr <decision>` | New ADR from the template | medium |
| `/depcheck <library> <purpose>` | License check under ADR-005; row in `docs/legal/dependencies.md` | medium |
| `/wrap` | End of session: CURRICULUM.md, issue, CURRENT TASK, commit | low |
| `/weekly` | Weekly log row and next week's goals | medium |
| `/retro <milestone>` | Milestone retro and the next stretch of modules | high |

These two are short enough to type by hand:

**Debug a failure** (Claude usually runs the test itself now; paste output only from your own terminal)
```
This test fails: <failing assertion/stack, ≤30 lines>. File: <path>.
Hypothesize the cause before changing anything.
```

**Learn while building (TypeScript / DSP)**
```
Explain <concept/code at path:line> as if I know C++ and Python well but not <TypeScript/this DSP topic>.
Compare it to the C++/Python equivalent. Keep it under 20 lines.
```

---

## 7. Time tracking cheat sheet

Optional alias (add to `~/.zshrc`): `alias tt=~/dev/bandlantern/tools/tt`

| Command | Does |
|---|---|
| `tt start [category] [note]` | Start a timer. Categories: build, test, design, docs, admin, learn, hw, env (default build) |
| `tt stop [end note]` | Stop and log (start and end notes are separate columns; both are optional, so an empty `end_note` is valid); prints today/week totals |
| `tt status` | What's running; today and week vs the 20 h target |
| `tt add 1h30m hw "zadig + sdr++ test" [--date 2026-10-24]` | Log time after the fact |
| `tt log [n]` | Last n entries |
| `tt report [week\|lastweek\|month\|phase\|all]` | Totals by day/category/phase; `phase`/`all` compare against the plan's estimates |
| `tt phase [N]` | Show or set the current phase |

The log lives in `progress/timelog.csv`, which is gitignored: your hours stay private. Run the tracker on the Mac only, so you never have two timers; log bench time afterwards with `tt add ... hw`. Back the file up with the rest of your local data.
