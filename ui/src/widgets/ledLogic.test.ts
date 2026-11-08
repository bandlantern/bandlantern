import { expect, test } from "vitest";
import { ledCells } from "./ledLogic";

test("digits light their segments and pad on the left", () => {
  const cells = ledCells("42", 4);
  expect(cells.map((c) => c.segments)).toEqual(["", "", "bcfg", "abdeg"]);
});

test("a point lights the cell before it", () => {
  const cells = ledCells("2.5", 2);
  expect(cells.map((c) => c.dp)).toEqual([true, false]);
});

test("minus is the middle bar; unknown text is blank", () => {
  expect(ledCells("-", 1)[0].segments).toBe("g");
  expect(ledCells("", 2).every((c) => c.segments === "" && !c.dp)).toBe(true);
});
