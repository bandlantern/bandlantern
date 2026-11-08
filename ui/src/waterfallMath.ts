// Pure helpers for the waterfall (no GL), so they can be unit tested.

/**
 * Ring texture row holding the row that is `age` rows older than the newest.
 * `head` is the texture row the NEXT incoming row will be written to.
 * The fragment shader uses the same formula.
 */
export function ringRowForAge(head: number, age: number, height: number): number {
  return (((head - 1 - age) % height) + height) % height;
}

export function advanceHead(head: number, height: number): number {
  return (head + 1) % height;
}

/** Palette index (0..255) for a dB value; the shader maps t = (db-min)/(max-min) the same way. */
export function dbToPaletteIndex(db: number, min_db: number, max_db: number, size = 256): number {
  const t = (db - min_db) / (max_db - min_db);
  if (!(t > 0)) {
    return 0; // also catches NaN
  }
  if (t >= 1) {
    return size - 1;
  }
  return Math.round(t * (size - 1));
}

/** Colormap control points: position 0..1, then r, g, b (0..255). Positions must increase. */
export type ColorStops = ReadonlyArray<readonly [number, number, number, number]>;

/** RGBA8, `size` entries, linearly interpolated between the stops. */
export function buildPalette(stops: ColorStops, size = 256): Uint8Array {
  const out = new Uint8Array(size * 4);
  for (let i = 0; i < size; i++) {
    const t = i / (size - 1);
    let s = 1;
    while (s < stops.length - 1 && t > stops[s][0]) {
      s++;
    }
    const a = stops[s - 1];
    const b = stops[s];
    const f = (t - a[0]) / (b[0] - a[0]);
    out[i * 4 + 0] = Math.round(a[1] + (b[1] - a[1]) * f);
    out[i * 4 + 1] = Math.round(a[2] + (b[2] - a[2]) * f);
    out[i * 4 + 2] = Math.round(a[3] + (b[3] - a[3]) * f);
    out[i * 4 + 3] = 255;
  }
  return out;
}

/** Canvas backing-store size in device pixels for a CSS size. */
export function backingSize(css_w: number, css_h: number, dpr: number): { w: number; h: number } {
  return { w: Math.max(1, Math.round(css_w * dpr)), h: Math.max(1, Math.round(css_h * dpr)) };
}
