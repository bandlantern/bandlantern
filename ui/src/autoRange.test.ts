import { expect, test } from "vitest";
import { AutoRange, CEILING_HEADROOM_DB, MIN_SPAN_DB, TIME_CONSTANT_S } from "./autoRange";
import type { SpectrumRow } from "./frames";

const BIN_COUNT = 2048;
const RATE_SPS = 2_400_000;
const CENTER_FREQ_HZ = 100_000_000;

/** Noise spread evenly over -103 ... -97 dBFS (deterministic), optionally with a wide carrier. */
function bins(carrier_db: number | null = null, carrier_bins = 128): Float32Array {
  const out = new Float32Array(BIN_COUNT);
  for (let i = 0; i < BIN_COUNT; i++) out[i] = -103 + (6 * ((i * 37) % BIN_COUNT)) / BIN_COUNT;
  if (carrier_db !== null) out.fill(carrier_db, 1000, 1000 + carrier_bins);
  return out;
}

function row(sample_index: number, bins_dbfs: Float32Array, center_freq_hz = CENTER_FREQ_HZ): SpectrumRow {
  return { seq: 0, sample_index, center_freq_hz, rate_sps: RATE_SPS, bin_count: bins_dbfs.length, bins_dbfs };
}

/** Feeds `seconds` of rows at `rows_per_s`, starting after `start_index`; returns the last sample_index. */
function feed(auto: AutoRange, start_index: number, seconds: number, rows_per_s: number, bins_dbfs: Float32Array) {
  const step = RATE_SPS / rows_per_s;
  let index = start_index;
  for (let r = 0; r < Math.round(seconds * rows_per_s); r++) {
    index += step;
    auto.update(row(index, bins_dbfs));
  }
  return index;
}

test("noise only: floor at the noise, span at the minimum", () => {
  const auto = new AutoRange();
  const range = auto.update(row(0, bins()))!;
  expect(Math.abs(range.min_db - -100)).toBeLessThanOrEqual(2);
  expect(range.max_db - range.min_db).toBe(MIN_SPAN_DB);
});

test("a strong carrier raises the ceiling over the time constant, not in one step", () => {
  const auto = new AutoRange();
  const start = auto.update(row(0, bins()))!;
  const target_max_db = -30 + CEILING_HEADROOM_DB;

  const after_one_row = auto.update(row(RATE_SPS / 30, bins(-30)))!;
  expect(after_one_row.max_db - start.max_db).toBeLessThan(0.1 * (target_max_db - start.max_db));

  feed(auto, RATE_SPS / 30, TIME_CONSTANT_S - 1 / 30, 30, bins(-30));
  const fraction = (auto.current!.max_db - start.max_db) / (target_max_db - start.max_db);
  expect(fraction).toBeCloseTo(1 - Math.exp(-1), 2);
  expect(auto.current!.min_db).toBeCloseTo(start.min_db, 6); // 6% of the bins don't move the floor
});

test("easing depends on time, not on the row rate", () => {
  const slow = new AutoRange();
  const fast = new AutoRange();
  slow.update(row(0, bins()));
  fast.update(row(0, bins()));
  feed(slow, 0, 1, 10, bins(-30));
  feed(fast, 0, 1, 60, bins(-30));
  expect(Math.abs(slow.current!.max_db - fast.current!.max_db)).toBeLessThan(0.5);
  expect(Math.abs(slow.current!.min_db - fast.current!.min_db)).toBeLessThan(0.5);
});

test("rows without finite bins leave the range unchanged", () => {
  const auto = new AutoRange();
  expect(auto.update(row(0, new Float32Array(BIN_COUNT).fill(NaN)))).toBeNull();
  const range = auto.update(row(80_000, bins()))!;
  expect(auto.update(row(160_000, new Float32Array(BIN_COUNT).fill(NaN)))).toEqual(range);
  expect(auto.update(row(240_000, new Float32Array(BIN_COUNT).fill(-Infinity)))).toEqual(range);
  expect(auto.update(row(320_000, new Float32Array(0)))).toEqual(range);
});

test("a retune or a new source snaps to the new levels", () => {
  const auto = new AutoRange();
  const index = feed(auto, 0, 5, 30, bins());
  const loud = new Float32Array(BIN_COUNT).fill(-30); // floor -30, ceiling -30 + 40

  const retuned = auto.update(row(index + 80_000, loud, CENTER_FREQ_HZ + 1_000_000))!;
  expect(retuned).toEqual({ min_db: -30, max_db: -30 + MIN_SPAN_DB });

  auto.update(row(index + 160_000, bins(), CENTER_FREQ_HZ + 1_000_000));
  const restarted = auto.update(row(0, loud, CENTER_FREQ_HZ + 1_000_000))!; // sample_index went back
  expect(restarted).toEqual({ min_db: -30, max_db: -30 + MIN_SPAN_DB });
});
