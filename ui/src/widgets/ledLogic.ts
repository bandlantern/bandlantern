// Seven-segment text for the LED readouts (DOM-free, so it can be unit tested).

/** Seven-segment patterns, segments a-g (a top, then clockwise, g middle). */
export const SEGMENTS: Record<number, string> = {
  0: "abcdef",
  1: "bc",
  2: "abdeg",
  3: "abcdg",
  4: "bcfg",
  5: "acdfg",
  6: "acdefg",
  7: "abc",
  8: "abcdefg",
  9: "abcdfg",
};

export interface LedCell {
  /** Lit segments, a-g; "" is a blank cell. */
  segments: string;
  /** Decimal point lit after this cell. */
  dp: boolean;
}

/**
 * Cells for `text`, right-aligned in `width` cells. Digits light their segments, "-" the middle
 * bar, "." lights the point of the cell before it, anything else is blank.
 */
export function ledCells(text: string, width: number): LedCell[] {
  const cells: LedCell[] = [];
  for (const ch of text) {
    if (ch === "." && cells.length > 0) {
      cells[cells.length - 1].dp = true;
    } else if (ch >= "0" && ch <= "9") {
      cells.push({ segments: SEGMENTS[Number(ch)], dp: false });
    } else if (ch === "-") {
      cells.push({ segments: "g", dp: false });
    } else if (ch !== ".") {
      cells.push({ segments: "", dp: false });
    }
  }
  while (cells.length < width) cells.unshift({ segments: "", dp: false });
  return cells;
}
