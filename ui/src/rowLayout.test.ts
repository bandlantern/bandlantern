import { expect, test } from "vitest";
import { REBASE_DISTANCE_HZ, RowLayouts } from "./rowLayout";

test("rows never written are empty (lower edge = upper edge)", () => {
  const layouts = new RowLayouts(4);
  expect([...layouts.offsets]).toEqual([0, 0, 0, 0, 0, 0, 0, 0]);
  layouts.rebase(1.3e9);
  expect([...layouts.offsets]).toEqual([0, 0, 0, 0, 0, 0, 0, 0]);
});

test("offsets from a reference near the band keep Hz precision at 1.3 GHz", () => {
  const layouts = new RowLayouts(4);
  layouts.rebase(1_300_000_000);
  layouts.set(1, 1_300_000_123.25, 1_300_048_123.25);
  expect(layouts.offsets[2]).toBe(123.25);
  expect(layouts.offsets[3]).toBe(48_123.25);
  // The same value as an absolute f32 is 128 Hz apart.
  expect(Math.fround(1_300_000_123.25)).not.toBe(1_300_000_123.25);
});

test("a full-band row and a zoom row keep their own ranges in the history", () => {
  const layouts = new RowLayouts(4);
  layouts.rebase(100_000_000);
  layouts.set(0, 98_800_000, 101_200_000); // old full-band row
  layouts.set(1, 100_052_000, 100_100_000); // newer zoom row
  expect([...layouts.offsets.slice(0, 4)]).toEqual([-1_200_000, 1_200_000, 52_000, 100_000]);
});

test("rebase recomputes every offset from the f64 edges, losing nothing", () => {
  const layouts = new RowLayouts(2);
  layouts.rebase(7_000_000);
  layouts.set(0, 7_000_123.5, 7_048_123.5);
  expect(layouts.needsRebase(7_000_000 + REBASE_DISTANCE_HZ)).toBe(false);
  expect(layouts.needsRebase(7_000_000 + REBASE_DISTANCE_HZ + 1)).toBe(true);
  layouts.rebase(14_000_000);
  expect(layouts.ref_hz).toBe(14_000_000);
  expect(layouts.offsets[0]).toBe(-6_999_876.5);
  layouts.rebase(7_000_000);
  expect(layouts.offsets[0]).toBe(123.5); // exact again
  expect(layouts.offsets[1]).toBe(48_123.5);
});

test("a row written under one reference is right after a re-base", () => {
  const layouts = new RowLayouts(2);
  layouts.set(1, 100_000_000.5, 100_001_000.5); // reference still 0: f32 offsets are coarse
  layouts.rebase(100_000_000);
  expect(layouts.offsets[2]).toBe(0.5);
  expect(layouts.offsets[3]).toBe(1000.5);
});
