// Waterfall colormaps. Original control points, chosen so brightness (WCAG relative luminance)
// never decreases with signal level: a stronger signal never looks dimmer than a weaker one.
// colormaps.test.ts checks that property.

import type { ColorStops } from "./waterfallMath";

export interface Colormap {
  id: string;
  name: string;
  stops: ColorStops;
}

export const COLORMAPS: readonly Colormap[] = [
  {
    // Multi-hue (navy, violet, magenta, orange, yellow) for telling weak levels apart.
    id: "spectrum",
    name: "Spectrum",
    stops: [
      [0.0, 4, 4, 14],
      [0.15, 30, 12, 72],
      [0.35, 104, 22, 120],
      [0.55, 196, 46, 86],
      [0.72, 240, 110, 40],
      [0.88, 250, 200, 70],
      [1.0, 252, 252, 224],
    ],
  },
  {
    // Black through LED red to warm white; matches the readouts.
    id: "ember",
    name: "Ember",
    stops: [
      [0.0, 6, 2, 2],
      [0.2, 52, 8, 6],
      [0.42, 128, 20, 10],
      [0.62, 214, 58, 16],
      [0.8, 255, 140, 40],
      [0.93, 255, 214, 130],
      [1.0, 255, 250, 235],
    ],
  },
  {
    // Deep navy through teal to ice white; matches Teal & Navy.
    id: "glacier",
    name: "Glacier",
    stops: [
      [0.0, 2, 6, 14],
      [0.2, 8, 32, 64],
      [0.42, 14, 84, 118],
      [0.62, 28, 150, 160],
      [0.8, 110, 214, 196],
      [0.93, 196, 244, 228],
      [1.0, 244, 255, 250],
    ],
  },
];
