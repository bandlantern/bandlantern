# ADR-013: Preact + TypeScript + Vite for the web UI

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-011, ADR-005; was ADR-023

## Context
The UI stays thin: state and commands over the engine API, with the waterfall in raw WebGL2
outside any framework. The deciding factors are debuggability, dependency weight, and
testability, with Claude writing most of the UI code.

## Decision
Use **Preact** (11.0.1, MIT, no runtime dependencies) with TypeScript and Vite, JSX through
Preact's automatic runtime (`ui/tsconfig.json`, `ui/vite.config.ts`). No React compat layer, no
state library: hooks and props until a real need shows up. DOM-free logic lives in plain `.ts`
modules with Vitest tests (e.g. `ui/src/widgets/ledLogic.ts`). Build and test tools (Vite,
Vitest, TypeScript) are not shipped; Preact is bundled.

## Alternatives considered
- **Svelte:** a compiler with its own file format; less transparent in DevTools and for
  AI-written code.
- **No framework (plain TS + DOM):** zero dependencies, but a growing app ends up hand-rolling a
  renderer.
- **React:** larger, and nothing here needs its ecosystem.

## Consequences
React-only libraries would need `preact/compat`; prefer framework-agnostic ones. Revisit if
Preact maintenance stalls or a needed component only exists for React.
