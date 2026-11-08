import { expect, test } from "vitest";
import type { SpectrumRow } from "./frames";
import { RowBus } from "./rowBus";

const row: SpectrumRow = { seq: 0, sample_index: 0, center_freq_hz: 100e6, rate_sps: 2e6, bin_count: 2, bins_dbfs: new Float32Array([-1, -2]) };

test("a subscribed listener gets each row with its geometry; unsubscribe stops it", () => {
  const bus = new RowBus();
  const seen: number[] = [];
  const stop = bus.subscribe((r, g) => seen.push(g.hi_hz - g.lo_hz + r.seq));
  bus.push(row);
  stop();
  bus.push(row);
  expect(seen).toEqual([2e6]);
});
