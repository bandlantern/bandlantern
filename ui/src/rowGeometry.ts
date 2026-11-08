// Where one spectrum row's bins lie on the frequency axis. frames.md: bin i is at
// centre + (i - N/2) * rate / N. Every row covers the whole band (this API has no views or zoom).

export interface RowGeometry {
  /** Lower edge of bin 0, Hz. Bin i spans bin_lo_hz + i * bin_width_hz, one bin_width_hz wide. */
  bin_lo_hz: number;
  bin_width_hz: number;
  /** The part of the row to show, Hz: all of it. */
  lo_hz: number;
  hi_hz: number;
}

export interface RowHeader {
  center_freq_hz: number;
  rate_sps: number;
  bin_count: number;
}

/** A bin's cell reaches half a bin either side of its frequency. */
export function rowGeometry(row: RowHeader): RowGeometry {
  const bin_width_hz = row.rate_sps / row.bin_count;
  const bin_lo_hz = row.center_freq_hz - row.rate_sps / 2 - bin_width_hz / 2;
  return { bin_lo_hz, bin_width_hz, lo_hz: bin_lo_hz, hi_hz: bin_lo_hz + row.rate_sps };
}

/**
 * The bins under [from_hz, to_hz] as fractional bin coordinates (a = start edge, b = end edge,
 * in bins from the row's start; not clipped to the row), or null if the range misses the row.
 */
export function binSpan(geom: RowGeometry, from_hz: number, to_hz: number): { a: number; b: number } | null {
  const lo_hz = Math.max(from_hz, geom.lo_hz);
  const hi_hz = Math.min(to_hz, geom.hi_hz);
  if (!(hi_hz > lo_hz)) return null;
  return { a: (lo_hz - geom.bin_lo_hz) / geom.bin_width_hz, b: (hi_hz - geom.bin_lo_hz) / geom.bin_width_hz };
}
