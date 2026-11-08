# Bandlantern UI

Browser UI for the A0 bench: Spectrum trace, Waterfall (WebGL2), Source, Status. Preact +
TypeScript + Vite (ADR-011..014). Contract: `docs/api/`. Node version: `.node-version`.

```sh
cd ui
npm ci
npm test         # vitest, incl. the golden-frame parse test
npm run build    # tsc + vite build -> ui/dist (the engine serves it, --static-root)
```

## With no engine: `?fake`

`npm run dev`, then open `http://localhost:5173/?fake`. A fake engine in the page streams
synthetic rows and answers `get_info`, `play`, `stop` and `set_fft`, so the waterfall and panels
can be seen without the engine. Other query options: `?theme=graphite|teal-navy|slate`,
`?colormap=<id>`.

## Dev server against a real engine

The engine only accepts its own origin, plus each `--dev-origin`. Start it so, with a token link:

```sh
<engine> --print-url --dev-origin http://localhost:5173     # prints http://127.0.0.1:8780/#token=...
cd ui && npm run dev
```

Open `http://localhost:5173/#token=<token from the engine's stderr>`. Two ways to reach the engine:

- **Proxy (default):** `vite.config.ts` forwards `/ws` to `ws://127.0.0.1:8780` with
  `changeOrigin` (so `Host` is the engine's) and leaves `Origin: http://localhost:5173` alone.
  For another engine port, change the proxy target.
- **Direct:** add `?engine=127.0.0.1:8780` to the URL (any `host:port`); the browser connects
  to the engine itself, `Origin` is the dev server's. Needs the same `--dev-origin`.

## Layout

- `src/client.ts`, `frames.ts`, `token.ts`, `connect.ts`: protocol (hello, commands, frame parser, token bootstrap).
- `src/waterfall.ts`, `spectrum.ts`, `autoRange.ts` and their `*Math.ts`: display.
- `src/fakeStream.ts`: synthetic rows and the fake engine.
- `src/app/`, `src/widgets/`, `themes/`: panels and token-driven themes.
- `src/api.generated.ts` is generated from the schema; never edit it.

Ported plumbing from the archive's UI; see the commit's Provenance line.
