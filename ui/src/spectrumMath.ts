// Pure helpers for the spectrum trace and its axes (no DOM), so they can be unit tested.

import { binSpan, type RowGeometry } from "./rowGeometry";

/**
 * Resamples a row onto out.length columns spanning [lo_hz, hi_hz] by max-hold, so a narrow
 * carrier stays visible however many bins share a column (ADR-017: reductions use max, not
 * mean). Where columns are narrower than bins, each shows the bin under it. NaN bins are
 * skipped; a column with none left, or outside the shown part of the row, is NaN. Writes into
 * out, no allocation.
 */
export function resampleMax(bins: Float32Array, geom: RowGeometry, lo_hz: number, hi_hz: number, out: Float32Array): void {
  const n = bins.length;
  const w = out.length;
  const column_hz = (hi_hz - lo_hz) / w;
  // Keeps a column edge that lands on a bin edge (to rounding) out of the bin beside it.
  const edge_slack = 1e-9;
  for (let c = 0; c < w; c++) {
    const span = binSpan(geom, lo_hz + c * column_hz, lo_hz + (c + 1) * column_hz);
    let peak = NaN;
    if (span) {
      const start = Math.max(0, Math.floor(span.a + edge_slack));
      const end = Math.min(n - 1, Math.max(start, Math.ceil(span.b - edge_slack) - 1));
      for (let i = start; i <= end; i++) {
        const v = bins[i];
        if (Number.isNaN(v)) continue;
        if (Number.isNaN(peak) || v > peak) peak = v;
      }
    }
    out[c] = peak;
  }
}

/**
 * Exponential average in place, avg += alpha * (row - avg), per bin. A non-finite avg bin
 * takes the row's value (start-up, or after -inf); a non-finite row bin leaves avg as it is.
 */
export function averageInto(avg: Float32Array, row: Float32Array, alpha: number): void {
  for (let i = 0; i < avg.length; i++) {
    const v = row[i];
    if (!Number.isFinite(v)) continue;
    avg[i] = Number.isFinite(avg[i]) ? avg[i] + alpha * (v - avg[i]) : v;
  }
}

export interface Ticks {
  step: number;
  values: number[];
}

/**
 * Round tick positions in [lo, hi]: the step is 1, 2, or 5 times a power of ten, the smallest
 * that gives at most max_count ticks.
 */
export function niceTicks(lo: number, hi: number, max_count: number): Ticks {
  if (!(hi > lo) || !(max_count >= 1)) return { step: 0, values: [] };
  const raw = (hi - lo) / max_count;
  const magnitude = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 5, 10].map((m) => m * magnitude).find((s) => s >= raw)!;
  const values: number[] = [];
  // Integer multiples of step, so 0.1 + 0.2 style sums never creep in; the slack keeps an end
  // tick that rounding puts a hair past hi (3 x 0.1).
  const slack = step * 1e-9;
  for (let k = Math.ceil(lo / step - 1e-9); k * step <= hi + slack; k++) values.push(k * step);
  return { step, values };
}

/** A tick label in MHz with as many decimals as the step needs (100 kHz steps: "100.3"). */
export function formatTickMHz(freq_hz: number, step_hz: number): string {
  const decimals = Math.max(0, Math.ceil(-Math.log10(step_hz / 1e6) - 1e-9));
  return (freq_hz / 1e6).toFixed(decimals);
}
