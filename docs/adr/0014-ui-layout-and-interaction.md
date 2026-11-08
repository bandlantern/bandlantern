# ADR-014: UI layout and interaction model

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-012; was ADR-033

## Context
SDR users fault floating windows and busy layouts and praise lean, direct-manipulation
receivers. The A0 bench has only a few controls, so the layout can be fixed panels.

## Decision
- **Fixed panels, no docking:** an app bar (theme, colormap), a display column (Spectrum trace
  over Waterfall) and a sidebar (Source, Status). On narrow screens the panels stack.
- **Controls stay in their panel:** source and FFT settings in Source; read-only state as red LED
  readouts in Status. An engine error is shown in the panel of the command that caused it, in
  plain words, never as a crash.
- **Interaction rules:** every control is a native element or has a keyboard equivalent; tabs
  only if a panel grows too large; prefer manipulating the display over text boxes once the
  display accepts input; single-letter shortcuts only outside text fields.

## Consequences
Panels are laid out by CSS grid, so there is no layout library to maintain. Revisit when more
panels arrive (decoders, tuning) and a fixed layout stops fitting.
