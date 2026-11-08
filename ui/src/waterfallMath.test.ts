import { expect, test } from "vitest";
import { advanceHead, backingSize, buildPalette, dbToPaletteIndex, ringRowForAge } from "./waterfallMath";

test("ring row index: newest is just before head, wraps at both ends", () => {
  expect(ringRowForAge(5, 0, 8)).toBe(4);
  expect(ringRowForAge(5, 4, 8)).toBe(0);
  expect(ringRowForAge(5, 5, 8)).toBe(7); // wrapped
  expect(ringRowForAge(0, 0, 8)).toBe(7); // head just wrapped
  expect(ringRowForAge(0, 7, 8)).toBe(0);
});

test("advanceHead wraps", () => {
  expect(advanceHead(6, 8)).toBe(7);
  expect(advanceHead(7, 8)).toBe(0);
});

test("dB maps to palette index with clamping", () => {
  expect(dbToPaletteIndex(-110, -110, -20)).toBe(0);
  expect(dbToPaletteIndex(-20, -110, -20)).toBe(255);
  expect(dbToPaletteIndex(-65, -110, -20)).toBe(128); // 0.5 * 255 = 127.5 -> 128
  expect(dbToPaletteIndex(-200, -110, -20)).toBe(0);
  expect(dbToPaletteIndex(0, -110, -20)).toBe(255);
  expect(dbToPaletteIndex(NaN, -110, -20)).toBe(0);
});

test("palette is opaque RGBA with the stop colours at the ends", () => {
  const p = buildPalette(
    [
      [0, 0, 0, 16],
      [0.5, 10, 20, 30],
      [1, 255, 255, 255],
    ],
    256,
  );
  expect(p.length).toBe(1024);
  expect(Array.from(p.subarray(0, 4))).toEqual([0, 0, 16, 255]);
  expect(Array.from(p.subarray(1020, 1024))).toEqual([255, 255, 255, 255]);
  for (let i = 3; i < p.length; i += 4) {
    expect(p[i]).toBe(255);
  }
});

test("backing size follows devicePixelRatio", () => {
  expect(backingSize(800, 600, 1)).toEqual({ w: 800, h: 600 });
  expect(backingSize(800.4, 600, 1.5)).toEqual({ w: 1201, h: 900 });
  expect(backingSize(0, 0, 2)).toEqual({ w: 1, h: 1 });
});
