# Bandlantern

A software-defined radio lab bench, built to learn DSP and radio protocols from their specs. Each
curriculum module adds a demodulator or decoder to the bench and ends with a devlog post.

- **How it's built:** I write the DSP and decoders myself, from specs and papers (clean-room,
  ADR-004). The plumbing (UI, protocol, build, CI) is AI-assisted; commits made with Claude carry
  a `Co-Authored-By` trailer. See ADR-001 and ADR-002.
- **Curriculum:** [docs/learning/CURRICULUM.md](docs/learning/CURRICULUM.md)
- **Devlog:** `site/` (published at https://bandlantern.com)
- **Hardware:** an RTL-SDR dongle (NESDR SMArt v5), optionally with a Ham It Up upconverter for HF. Receive only.

## Layout

| Path | Contents |
|---|---|
| `engine/` | C++20 headless engine (DSP, sources, buffers, decoders, API server) |
| `site/` | Devlog (Astro) |
| `tools/` | Dev scripts: env check, time tracker |
| `docs/` | ADRs, curriculum, dev workflow, legal |

More directories appear as the modules need them.

## Build (macOS / Linux)

Needs CMake ≥ 3.25, Ninja, a C++20 compiler, and vcpkg (`VCPKG_ROOT` set).

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

Other presets: `macos-asan`, `macos-tsan`, `linux-debug`, `linux-asan`, `linux-tsan`, `win-msvc`.

## License

Code: MIT ([LICENSE](LICENSE)). Devlog text: CC BY 4.0.
