import { expect, test } from "vitest";
import { binSpan, rowGeometry } from "./rowGeometry";

test("row covers rate_sps, DC bin centred on center_freq_hz", () => {
  const g = rowGeometry({ center_freq_hz: 100e6, rate_sps: 8, bin_count: 8 });
  expect(g.bin_width_hz).toBe(1);
  expect(g.hi_hz - g.lo_hz).toBe(8);
  // bin 4 (= bin_count / 2) spans centre +/- half a bin
  expect(g.bin_lo_hz + 4 * g.bin_width_hz).toBe(100e6 - 0.5);
});

test("binSpan clips to the row and misses outside it", () => {
  const g = rowGeometry({ center_freq_hz: 0, rate_sps: 8, bin_count: 8 });
  expect(binSpan(g, g.lo_hz - 10, g.hi_hz + 10)).toEqual({ a: 0, b: 8 });
  expect(binSpan(g, g.hi_hz + 1, g.hi_hz + 2)).toBeNull();
});
