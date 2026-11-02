# ADR-003: Engine in C++20

- **Status:** Accepted
- **Date:** 2026-10-25
- **Related:** was ADR-001

## Context
The project is wide (DSP, drivers, UI, networking, protocols) and built by one half-time developer whose strongest languages are C++ and Python. The SDR ecosystem (SoapySDR, Hamlib, libusb, ONNX Runtime, VkFFT, Opus) is native C/C++.

## Decision
Write the engine in C++20 with CMake presets + vcpkg (manifest mode). Recover Rust-style safety with `std::span`/RAII, `-Wall -Wextra`, clang-tidy, ASan/UBSan/TSan in CI, and fuzzing every decoder.

## Consequences
No FFI layers for core dependencies. Memory-safety discipline is enforced by tooling, not the language. Rust may be tried later on an isolated component behind a C ABI.
