// Auto color range for the waterfall: follows the noise floor and the strong signals in recent
// rows so the colormap spans the levels that matter. Display-only, per view; no GL, so it can be
// unit tested.

import type { SpectrumRow } from "./frames";

export interface DbRange {
  min_db: number;
  max_db: number;
}

// Percentiles come from a 1 dB histogram allocated once, so a row costs one pass and no sort.
const HIST_MIN_DB = -200; // dBFS; levels outside the histogram count in its end buckets
const HIST_MAX_DB = 20;
const HIST_STEP_DB = 1;

// The floor sits in the noise: most bins of a typical band are noise, so a low percentile is a
// noise-floor estimate that a few strong signals don't move.
export const FLOOR_PERCENTILE = 0.2;
// The ceiling follows signals wide enough to cover 0.5% of the bins (a broadcast FM station at
// 2.4 MS/s covers ~8%); a narrower carrier just saturates the top color.
export const CEILING_PERCENTILE = 0.995;
export const CEILING_HEADROOM_DB = 6;
// An 8-bit RTL shows roughly this much range after FFT gain; a smaller span on an empty band
// stretches the noise into full-contrast speckle.
export const MIN_SPAN_DB = 40;
// Slow on both edges so a carrier keying up doesn't pump the colors.
export const TIME_CONSTANT_S = 2;

export class AutoRange {
  private readonly hist = new Uint32Array(Math.round((HIST_MAX_DB - HIST_MIN_DB) / HIST_STEP_DB) + 1);
  private range: DbRange | null = null;
  private last_sample_index = 0;
  private last_center_freq_hz = 0;
  private last_rate_sps = 0;

  /** Current range, or null before the first row with finite bins. */
  get current(): DbRange | null {
    return this.range;
  }

  /**
   * Feed one row; returns the updated range. Time comes from the row's sample_index, so replay
   * gives the same ranges as live. A retune, rate change, or sample_index going backwards
   * (new source) snaps to the new levels instead of easing in from the old band.
   */
  update(row: SpectrumRow): DbRange | null {
    const span_changed = row.center_freq_hz !== this.last_center_freq_hz || row.rate_sps !== this.last_rate_sps;
    const dt_s = (row.sample_index - this.last_sample_index) / row.rate_sps;
    this.last_center_freq_hz = row.center_freq_hz;
    this.last_rate_sps = row.rate_sps;
    this.last_sample_index = row.sample_index;

    const target = this.target(row.bins_dbfs);
    if (!target) return this.range;
    if (!this.range || span_changed || !(dt_s > 0)) {
      this.range = target;
      return this.range;
    }
    // Same factor on both edges: the result is a blend of two ranges that both span at least
    // MIN_SPAN_DB, so it does too.
    const alpha = 1 - Math.exp(-dt_s / TIME_CONSTANT_S);
    this.range = {
      min_db: this.range.min_db + (target.min_db - this.range.min_db) * alpha,
      max_db: this.range.max_db + (target.max_db - this.range.max_db) * alpha,
    };
    return this.range;
  }

  private target(bins_dbfs: Float32Array): DbRange | null {
    this.hist.fill(0);
    let count = 0;
    for (const db of bins_dbfs) {
      if (!Number.isFinite(db)) continue;
      const i = Math.round((db - HIST_MIN_DB) / HIST_STEP_DB);
      this.hist[Math.min(Math.max(i, 0), this.hist.length - 1)]++;
      count++;
    }
    if (count === 0) return null;
    const min_db = this.percentile(FLOOR_PERCENTILE, count);
    const max_db = Math.max(this.percentile(CEILING_PERCENTILE, count) + CEILING_HEADROOM_DB, min_db + MIN_SPAN_DB);
    return { min_db, max_db };
  }

  /** Level of the bucket holding the bin at rank fraction * count. */
  private percentile(fraction: number, count: number): number {
    const rank = fraction * count;
    let seen = 0;
    for (let i = 0; i < this.hist.length; i++) {
      seen += this.hist[i];
      if (seen > rank) return HIST_MIN_DB + i * HIST_STEP_DB;
    }
    return HIST_MAX_DB;
  }
}
