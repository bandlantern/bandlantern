import { describe, expect, it } from "vitest";
import { COLORMAPS } from "./colormaps";
import { relativeLuminance } from "./tokens";
import { buildPalette } from "./waterfallMath";

const hex = (p: Uint8Array, i: number) =>
  "#" + [p[i * 4], p[i * 4 + 1], p[i * 4 + 2]].map((c) => c.toString(16).padStart(2, "0")).join("");

describe.each(COLORMAPS.map((c) => [c.name, c] as const))("colormap %s", (_, colormap) => {
  it("has stops from 0 to 1 in increasing order", () => {
    const positions = colormap.stops.map((s) => s[0]);
    expect(positions[0]).toBe(0);
    expect(positions[positions.length - 1]).toBe(1);
    for (let i = 1; i < positions.length; i++) expect(positions[i]).toBeGreaterThan(positions[i - 1]);
  });

  it("never gets darker as the level rises", () => {
    const palette = buildPalette(colormap.stops, 256);
    for (let i = 1; i < 256; i++) {
      expect(relativeLuminance(hex(palette, i))).toBeGreaterThanOrEqual(relativeLuminance(hex(palette, i - 1)));
    }
  });

  it("spans dark to bright", () => {
    const palette = buildPalette(colormap.stops, 256);
    expect(relativeLuminance(hex(palette, 0))).toBeLessThan(0.01);
    expect(relativeLuminance(hex(palette, 255))).toBeGreaterThan(0.85);
  });
});

it("colormap ids are unique", () => {
  expect(new Set(COLORMAPS.map((c) => c.id)).size).toBe(COLORMAPS.length);
});
