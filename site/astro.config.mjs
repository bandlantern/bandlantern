import { defineConfig } from "astro/config";

// Static output only: the devlog is plain HTML/CSS on Cloudflare Pages (ADR-002).
export default defineConfig({
  site: "https://bandlantern.com",
  output: "static",
});
