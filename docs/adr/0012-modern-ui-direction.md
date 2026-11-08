# ADR-012: Modern palettes and flat panels

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-011; was ADR-022

## Context
A first bench rendered a classic-Windows look (gray palette, two-pixel bevels, etched group
boxes, gradient title bars, the app drawn as a window on a desktop). It was well made but read as
dated, and the page was taller than the screen.

## Decision
- **Palettes:** **Graphite** (dark neutral), **Teal & Navy** (dark blue, teal accent) and
  **Slate** (light), as token files in `ui/themes/`. Every text/background pair passes WCAG 2.2
  AA (4.5:1); focus and accent pass 3:1 against the surface (`ui/src/tokens.test.ts`).
- **No window metaphor:** no desktop, outer frame, gradient title bar, or bevel. The app fills
  the browser viewport: an app bar and flat titled panels. A panel shows its active state with an
  accent bar on its header while focus is inside it.
- **Edges:** one-pixel borders, square corners.
- **Kept:** red seven-segment LED readouts (the instrument identity), the token pipeline
  (JSON to CSS variables), keyboard focus everywhere, original assets only.
- **Waterfall colormaps:** original, switchable live; each one's brightness never decreases with
  level (`ui/src/colormaps.test.ts`), so a stronger signal never looks dimmer than a weaker one.
- **Fit the viewport:** wide screens use the full window height without a page scrollbar; the
  waterfall absorbs spare height. Narrow widths stack and may scroll.

## Consequences
Panels have plain headers with no title-bar chrome to keep in sync. Revisit if users ask for a
classic look (it could return as an optional theme).
