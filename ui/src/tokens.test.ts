import { describe, expect, it } from "vitest";
import graphite from "../themes/graphite.json";
import slate from "../themes/slate.json";
import tealNavy from "../themes/teal-navy.json";
import { contrastRatio, flattenTokens, themeCss, type Theme } from "./tokens";

describe("flattenTokens", () => {
  it("joins group and leaf names into CSS variable names", () => {
    const vars = flattenTokens({ color: { surface: "#c0c0c0", shadow: "#00000080" }, size: { "space-1": "4px" } });
    expect([...vars]).toEqual([
      ["--color-surface", "#c0c0c0"],
      ["--color-shadow", "#00000080"],
      ["--size-space-1", "4px"],
    ]);
  });

  it.each([
    [{ color: { surface: "gray" } }],
    [{ color: { surface: "#c0c0c0; }" } }],
    [{ color: { surface: "#c0c0c0a" } }],
    [{ size: { pad: "4" } }],
    [{ size: { pad: "4em" } }],
    [{ font: { ui: "x; } body { display: none" } }],
    [{ other: { x: "1px" } }],
    [{ color: { Surface: "#c0c0c0" } }],
  ])("rejects %j", (tree) => {
    expect(() => flattenTokens(tree as never)).toThrow();
  });
});

describe("themeCss", () => {
  it("scopes every token to the theme id", () => {
    const theme: Theme = { id: "t", name: "T", tokens: { size: { gap: "4px" }, color: { text: "#000000" } } };
    expect(themeCss(theme)).toBe(':root[data-theme="t"] {\n  --size-gap: 4px;\n  --color-text: #000000;\n}\n');
  });

  it("rejects a theme id that could break the selector", () => {
    expect(() => themeCss({ id: 't"]', name: "T", tokens: {} })).toThrow();
  });
});

describe("contrastRatio", () => {
  it("matches the WCAG endpoints and a known mid value", () => {
    expect(contrastRatio("#000000", "#ffffff")).toBeCloseTo(21, 5);
    expect(contrastRatio("#ffffff", "#ffffff")).toBeCloseTo(1, 5);
    expect(contrastRatio("#777777", "#ffffff")).toBeCloseTo(4.48, 2);
  });
});

// Text over the background it is drawn on: WCAG 2.2 AA, 4.5:1 for normal-size text.
const TEXT_PAIRS = [
  ["text", "bg"],
  ["text", "surface"],
  ["text", "surface-raised"],
  ["text", "surface-sunken"],
  ["text-muted", "surface"],
  ["text-muted", "surface-raised"],
  ["select-text", "select-bg"],
  ["accent-text", "accent"],
  ["led-on", "led-bg"],
];

for (const theme of [graphite, tealNavy, slate] as Theme[]) {
  describe(`${theme.name} theme`, () => {
    it("produces CSS", () => {
      expect(themeCss(theme)).toContain(`[data-theme="${theme.id}"]`);
    });

    it.each(TEXT_PAIRS)("%s on %s is at least 4.5:1", (fg, bg) => {
      const vars = flattenTokens(theme.tokens);
      expect(contrastRatio(vars.get(`--color-${fg}`)!, vars.get(`--color-${bg}`)!)).toBeGreaterThanOrEqual(4.5);
    });

    // Focus rings and the accent must stand out from the surface (WCAG 2.2 1.4.11, 3:1).
    it("focus and accent are at least 3:1 against the surface", () => {
      const vars = flattenTokens(theme.tokens);
      expect(contrastRatio(vars.get("--color-focus")!, vars.get("--color-surface")!)).toBeGreaterThanOrEqual(3);
      expect(contrastRatio(vars.get("--color-accent")!, vars.get("--color-surface")!)).toBeGreaterThanOrEqual(3);
    });

    // Doctor status lamps (a word always goes with them): 3:1 like other UI components.
    it.each(["status-ok", "status-warn", "status-fail"])("%s is at least 3:1 against the surface", (lamp) => {
      const vars = flattenTokens(theme.tokens);
      expect(contrastRatio(vars.get(`--color-${lamp}`)!, vars.get("--color-surface")!)).toBeGreaterThanOrEqual(3);
    });
  });
}
