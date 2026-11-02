# Bandlantern devlog

Astro static site (ADR-002). Posts live in `src/content/posts/*.md`, one per curriculum module
(`docs/learning/CURRICULUM.md`). A post with `draft: true` is never built.

- Dev server: `cd site && npm install && npm run dev` → http://localhost:4321
- Build: `npm run build` → `site/dist/`
- Figures: generate them with scripts in `tools/py/learning/<module>/` and commit the output under
  `site/public/figures/<module>/`, so every plot can be regenerated from the repo.

## Deploying to Cloudflare Pages (owner, one-time)
1. Cloudflare dashboard → Workers & Pages → Create → Pages → Connect to Git → pick this repo
   (`bandlantern/bandlantern`).
2. Build settings: framework preset **Astro**, root directory `site`, build command
   `npm run build`, output directory `dist`.
3. Environment variable `NODE_VERSION` = `24` (Astro 7.4.5 needs Node ≥ 22.23.4).
4. Domain `bandlantern.com` (registered at Porkbun): Cloudflare → Add a domain (free plan), let it
   import the DNS records, and **check that the Bluesky `_atproto` TXT record and any MX/email
   records came across** before switching. Then Porkbun → Authoritative nameservers → the two
   Cloudflare nameservers. Once the zone is active: Pages project → Custom domains → add
   `bandlantern.com`, plus `www.bandlantern.com` redirecting to it.
   (No nameserver move: use `devlog.bandlantern.com` as a CNAME to `<project>.pages.dev` instead.)

Every push to `main` then redeploys. Drafts stay off the site until `draft: false`.

Text is CC BY 4.0; code is MIT (`../LICENSE`).
