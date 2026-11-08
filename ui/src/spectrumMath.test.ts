import { expect, test } from "vitest";
import { averageInto, formatTickMHz, niceTicks, resampleMax } from "./spectrumMath";
import { rowGeometry } from "./rowGeometry";

// 8 bins one Hz wide, centred on 0 Hz: the full row spans -4.5 ... 3.5 Hz.
const row8 = rowGeometry({ center_freq_hz: 0, rate_sps: 8, bin_count: 8 });

test("resampleMax keeps the peak of every bin group", () => {
  const bins = new Float32Array([-100, -100, -30, -100, -90, -80, -100, -100]);
  const out = new Float32Array(4);
  resampleMax(bins, row8, row8.lo_hz, row8.hi_hz, out);
  expect([...out]).toEqual([-100, -30, -80, -100]);

  // 3 columns over 8 bins: groups 0-2, 2-5, 5-7 (a column touching a bin counts it).
  const three = new Float32Array(3);
  resampleMax(bins, row8, row8.lo_hz, row8.hi_hz, three);
  expect([...three]).toEqual([-30, -30, -80]);
});

test("resampleMax with more columns than bins repeats each bin", () => {
  const geom = rowGeometry({ center_freq_hz: 0, rate_sps: 3, bin_count: 3 });
  const out = new Float32Array(6);
  resampleMax(new Float32Array([-10, -20, -30]), geom, geom.lo_hz, geom.hi_hz, out);
  expect([...out]).toEqual([-10, -10, -20, -20, -30, -30]);
});

test("resampleMax skips NaN; an all-NaN column is NaN; no bins is all NaN", () => {
  const out = new Float32Array(2);
  resampleMax(new Float32Array([NaN, -50, NaN, NaN]), rowGeometry({ center_freq_hz: 0, rate_sps: 4, bin_count: 4 }), -2.5, 1.5, out);
  expect(out[0]).toBe(-50);
  expect(out[1]).toBeNaN();
  resampleMax(new Float32Array(0), row8, row8.lo_hz, row8.hi_hz, out);
  expect(out[0]).toBeNaN();
});

test("resampleMax over a view: magnifies part of the row, NaN outside the row", () => {
  const bins = new Float32Array([-100, -100, -30, -100, -90, -80, -100, -100]);
  // View -2.5 ... 1.5 Hz = bins 2 ... 5 over 4 columns: one bin per column.
  const out = new Float32Array(4);
  resampleMax(bins, row8, -2.5, 1.5, out);
  expect([...out]).toEqual([-30, -100, -90, -80]);
  // View twice the row's width: the outer columns are outside it.
  const wide = new Float32Array(4);
  resampleMax(bins, row8, -8.5, 7.5, wide);
  expect(wide[0]).toBeNaN();
  expect(wide[1]).toBe(-30);
  expect(wide[3]).toBeNaN();
});

test("averageInto eases toward the row and starts from the first finite value", () => {
  const avg = new Float32Array([NaN, -100, -100, -Infinity]);
  averageInto(avg, new Float32Array([-60, -60, NaN, -70]), 0.25);
  expect([...avg]).toEqual([-60, -90, -100, -70]);
});

test("niceTicks: 1-2-5 steps, inside the range", () => {
  expect(niceTicks(99_350_000, 101_750_000, 8)).toEqual({
    step: 500_000,
    values: [99_500_000, 100_000_000, 100_500_000, 101_000_000, 101_500_000],
  });
  expect(niceTicks(-110, -20, 6).step).toBe(20);
  expect(niceTicks(-110, -20, 6).values).toEqual([-100, -80, -60, -40, -20]);
  expect(niceTicks(0, 1, 10).values.length).toBe(11); // 0.1 steps, both ends included
  expect(niceTicks(0.1, 0.3, 2).values.length).toBe(3); // 3 x 0.1 rounds past 0.3; still a tick
  expect(niceTicks(5, 5, 4).values).toEqual([]);
});

test("tick labels show the decimals the step needs", () => {
  expect(formatTickMHz(100_500_000, 500_000)).toBe("100.5");
  expect(formatTickMHz(100_000_000, 500_000)).toBe("100.0");
  expect(formatTickMHz(14_074_000, 1_000)).toBe("14.074");
  expect(formatTickMHz(14_070_000, 2_000)).toBe("14.070");
  expect(formatTickMHz(7_000_000, 1_000_000)).toBe("7");
  expect(formatTickMHz(7_200_000, 200_000)).toBe("7.2");
});
