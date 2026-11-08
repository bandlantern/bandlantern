# Third-Party Dependencies

Direct dependencies only. "Shipped" means the code ends up in a release artifact.
Update this table in the same commit that adds, removes, or upgrades a dependency.
`Package` is the manifest id (`vcpkg:`, `npm:`, `pip:`) that the license scan matches rows by.

The SBOM and license scan (`tools/py/sbom.py` in CI) arrives with the first shipped dependency;
until then this table is checked by hand.

| Name | Package | Version | License | Used by | Shipped | Linking | Added |
|---|---|---|---|---|---|---|---|
| Astro | `npm:astro` | 7.4.5 | MIT (transitive: 152 MIT, 10 ISC, 8 BSD-2, 5 Apache-2.0, 3 BlueOak, 3 BSD-3, 2 CC0, 2 MPL-2.0 lightningcss, 1 LGPL-3.0+ sharp-libvips) | `site/` devlog build | No (build tool) | n/a | 2026-11-02 |
| gitleaks | — (CI download) | 8.30.1 | MIT | pre-commit hook + CI secret scan | No (dev tool) | n/a | 2026-10-25 |
| Boost.Asio | `vcpkg:boost-asio` | 1.92.0 | BSL-1.0 (transitive `boost-*` ports: all BSL-1.0) | engine HTTP + WebSocket server (`engine/api/`) | Yes | static (mostly header-only; `boost-context` compiled) | 2026-11-08 |
| Boost.Beast | `vcpkg:boost-beast` | 1.92.0 | BSL-1.0 | engine HTTP + WebSocket server (`engine/api/`) | Yes | static (header-only) | 2026-11-08 |
| nlohmann-json | `vcpkg:nlohmann-json` | 3.12.0#2 | MIT (notice: LICENSE.MIT, © 2013-2025 Niels Lohmann) | engine JSON messages (`engine/api/`) | Yes | static (header-only) | 2026-11-08 |
