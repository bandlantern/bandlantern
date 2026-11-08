# Dependency and License Map

Every dependency we use **or plan to use**, its license, and how it crosses the process boundary.
Source rule: ADR-005 (the code is MIT; no GPL/AGPL in the main process).

- Adopted dependencies with exact versions live in [THIRD_PARTY.md](THIRD_PARTY.md); update
  that table in the same commit that adds a dependency. This file adds the planned ones and
  the reasoning.
- "License" means what we believe it is. It is not verified until the **Verified** column has
  a date and a link to the license file at the pinned version.
- Ask before adopting anything in the "Planned" table (CLAUDE.md rule). Rows verified in the
  predecessor repo keep their dates; they're adopted here when the code that uses them is ported.

## Process boundary model

| Boundary | What may cross it | Examples |
|---|---|---|
| Main process (engine, UI shell) | Permissive licenses; LGPL **dynamic only**, user-replaceable | pocketfft, Boost.Beast, nlohmann-json |
| Separate helper executable | GPL programs, talking over a socket/pipe at arm's length | stock `rtl_tcp` (stage 1), a custom helper (later, own public repo) |
| Build/test only (not shipped) | Anything permissive; GPL tools are OK if they produce no shipped output | Catch2, Vite, pytest, gitleaks |
| Never | GPL/AGPL in process; FFTW; copied GPL source; forum snippets (CC BY-SA) | |

## License scan policy

When `tools/py/sbom.py` is ported, it runs in CI on every package vcpkg and npm install, transitive
ones included. Its allowlists encode the boundary model:

- **Shipped:** 0BSD, Apache-2.0, BSD-2-Clause, BSD-3-Clause, BSL-1.0, ISC, MIT, MIT-0,
  Unlicense, Zlib. An `OR` expression passes if one choice is allowed; `AND` needs both.
- **Build/test only:** the above plus MPL-2.0 (file-level weak copyleft; we run the tools
  unmodified and ship none of their files). Today: `lightningcss` and its per-platform
  binaries, pulled in by Vite (2026-10-30).
- Everything else fails the build, including LGPL (allowed only dynamically and user-replaceable,
  which a license string cannot show), GPL/AGPL, `NOASSERTION`, and licenses not yet seen. Adding
  a license to a list needs a note here first.
- vcpkg ports named `vcpkg-*` (CMake helpers) and ports THIRD_PARTY.md marks "Shipped: No"
  count as build-only; their transitive ports count as shipped (stricter, the safe direction).

## Adopted (summary; details in THIRD_PARTY.md)

| Name | License | Shipped | Boundary | Verified |
|---|---|---|---|---|
| Catch2 | BSL-1.0 AND BSD-3-Clause (CMake scripts © Kitware, per the vcpkg port and its installed `copyright`) | No | Build/test | |
| gitleaks | MIT | No | Dev tool | |
| Astro (devlog site) | MIT; build-time tree also has LGPL-3.0-or-later libvips (loaded dynamically by sharp for image optimization) and MPL-2.0 lightningcss, neither in the site output | No (the built site ships only our HTML/CSS and any island JS) | Build/test | |

## Planned / candidate (not adopted)

| Name | Believed license | Planned use | Boundary | Archive phase | Notes | Verified |
|---|---|---|---|---|---|---|
| `rtl_tcp` (upstream librtlsdr) | GPL-2.0-or-later | RTL IQ source, stage 1 | Separate process, **user-installed, not shipped** | 0 | Our client is written from the protocol description only | |
| librtlsdr | GPL-2.0-or-later | Stage 2 helper only | Separate process, own public repo | 1–2 | Never in main process | |
| pocketfft | BSD-3-Clause | CPU FFT | Main process, static (header-only) | 0–1 | Header-only C++ variant (`cpp` branch); vcpkg port `pocketfft` 2024-11-30 pins `0fa0ef5`. Notice: ship LICENSE.md text (© 2010-2018 Max-Planck-Society) in third-party notices | 2026-10-26 [LICENSE.md @ 0fa0ef5](https://github.com/mreineck/pocketfft/blob/0fa0ef591e38c2758e3184c6c23e497b9f732ffa/LICENSE.md) |
| Boost.Beast + Boost.Asio | BSL-1.0 | Engine HTTP + WebSocket server (loopback) | Main process, static (header-only) | 0–1 | vcpkg ports `boost-beast`/`boost-asio` 1.92.0 (plus BSL-1.0 Boost deps). No TLS, no OpenSSL. BSL: no notice required for object-code-only distribution; list in notices anyway. Re-checked 2026-11-08 at the vcpkg baseline `817892b`: all ~55 transitive `boost-*` ports install the BSL-1.0 `copyright` text; `boost-context` (pulled in by `asio_spawn`) is compiled, linked statically, also BSL-1.0 | 2026-10-26, re-checked 2026-11-08: [Beast LICENSE_1_0.txt @ boost-1.92.0](https://github.com/boostorg/beast/blob/boost-1.92.0/LICENSE_1_0.txt), [Asio LICENSE_1_0.txt @ boost-1.92.0](https://github.com/boostorg/asio/blob/boost-1.92.0/LICENSE_1_0.txt) |
| nlohmann-json | MIT | Engine JSON messages | Main process, static (header-only) | 0–1 | vcpkg port `nlohmann-json` 3.12.0. Notice: ship LICENSE.MIT text (© 2013-2025 Niels Lohmann). Re-checked 2026-11-08 at the vcpkg baseline `817892b` (port 3.12.0#2) | 2026-10-26, re-checked 2026-11-08: [LICENSE.MIT @ v3.12.0](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) |
| Preact | MIT | UI rendering (`ui/`) | Shipped in the UI bundle | 0 | npm `preact` 11.0.1, no dependencies of its own. Notice: ship LICENSE (© 2015-present Jason Miller). Approved 2026-11-08 for the A0 UI | 2026-11-08 LICENSE in the npm 11.0.1 package |
| dockview-core | MIT | UI docking panels | Shipped in the UI bundle | Later | npm 8.5.1, no dependencies; LICENCE.md © 2021 mathuo. Checked 2026-11-08 but **not adopted**: the pre-A0 UI uses fixed panels | 2026-11-08 LICENCE.md in the npm 8.5.1 package |
| Vite + Vitest | MIT | UI build and tests | Build/test | 0 | npm `vite` 8.3.5, `vitest` 5.0.5. Transitive tree MIT/Apache-2.0/ISC/BSD-3-Clause plus MPL-2.0 `lightningcss` (build-only, per the scan policy above) | 2026-11-08 LICENSE.md in the npm packages |
| TypeScript | Apache-2.0 | UI type checking and build | Build/test | 0 | npm `typescript` 7.0.2; emits our code only, nothing of its own ships | 2026-11-08 LICENSE in the npm 7.0.2 package |
| VkFFT | MIT | GPU FFT | Main process | Later | | |
| ONNX Runtime | MIT | Modulation recognition | Main process | Later | Check bundled third-party notices | |
| miniaudio | Unlicense OR MIT-0 (licensee chooses; we use MIT-0) | Audio output | Main process, static (single C source) | 0 (spike 2) | **Adopted 2026-10-29.** vcpkg port `miniaudio` 0.11.25. No notice required by either alternative; list in notices anyway. On Linux it loads ALSA/PulseAudio client libraries (LGPL-2.1) at run time with `dlopen`, i.e. dynamic and user-replaceable (ADR-005 OK); Windows WASAPI and macOS Core Audio are system APIs | 2026-10-29 [LICENSE @ 0.11.25](https://github.com/mackron/miniaudio/blob/0.11.25/LICENSE) |
| Electron | MIT (+ Chromium component licenses) | Desktop shell option; shell spike (#14) | Shell process (separate from the engine) | 1 | Checked v45.1.0 (npm `license: MIT`). Ships Chromium's third-party notices as `LICENSES.chromium.html` in the app folder; ship it and the Electron LICENSE (© Electron contributors, © 2013-2020 GitHub Inc.). FFmpeg is a separate shared library (`ffmpeg.dll` / `libffmpeg.so` / `libffmpeg.dylib`), LGPL-2.1 build, user-replaceable, so dynamic (ADR-005 OK) (verify the LGPL config before shipping). Default FFmpeg includes H.264/AAC decoders (patent question, not copyright); Electron publishes a swap-in `ffmpeg-v<ver>-<os>.zip` without proprietary codecs, use it unless we need them. Spike only until the shell ADR | 2026-10-29 [LICENSE @ v45.1.0](https://github.com/electron/electron/blob/v45.1.0/LICENSE) |
| Fonts | Prefer SIL OFL-1.1 | UI | Asset | 0 (spike 3) | No proprietary system fonts | |

## Comparators (black-box only, never read, linked, or shipped)

GNU Radio, SDR++, SDRangel, GQRX, WSJT-X, JTDX, fldigi, Dire Wolf, multimon-ng. We may run
them to compare outputs on our own recordings. We don't read their source (CLAUDE.md
clean-room rule, ADR-004).
