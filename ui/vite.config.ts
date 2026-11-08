import { defineConfig } from "vite";

export default defineConfig({
  // Preact's automatic JSX runtime (tsconfig.json says the same for type checking).
  oxc: { jsx: { runtime: "automatic", importSource: "preact" } },
  server: {
    // `npm run dev`: forward the UI's `ws://<page host>/ws` to a local engine (ui/README.md).
    // changeOrigin makes Host the engine's own (README "Request checks"); the browser's Origin
    // (http://localhost:5173) passes through, so start the engine with --dev-origin.
    proxy: { "/ws": { target: "ws://127.0.0.1:8780", ws: true, changeOrigin: true } },
  },
});
