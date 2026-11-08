// Theme tokens (JSON) -> CSS custom properties. A theme is a tree of groups; the leaf at
// color.surface becomes `--color-surface`. Each theme is scoped to `:root[data-theme="<id>"]`.
// Values are validated by group so a theme file can't inject CSS.

export type TokenTree = { [key: string]: string | TokenTree };

export interface Theme {
  id: string;
  name: string;
  tokens: TokenTree;
}

const NAME = /^[a-z][a-z0-9-]*$/;
const HEX_COLOR = /^#[0-9a-f]{6}([0-9a-f]{2})?$/i; // #rrggbb or #rrggbbaa
const PX = /^\d+(\.\d+)?px$/;
// Fonts: no `;{}<>\` so a value can't close the declaration or the rule.
const SAFE_VALUE = /^[A-Za-z0-9#(),.%' -]+$/;

/** Flattens a token tree into CSS variable names -> values, validating every leaf. */
export function flattenTokens(tree: TokenTree, path: string[] = []): Map<string, string> {
  const out = new Map<string, string>();
  for (const [key, value] of Object.entries(tree)) {
    if (!NAME.test(key)) throw new Error(`bad token name "${key}"`);
    const leaf_path = [...path, key];
    if (typeof value !== "string") {
      for (const [k, v] of flattenTokens(value, leaf_path)) out.set(k, v);
      continue;
    }
    checkValue(leaf_path, value);
    out.set("--" + leaf_path.join("-"), value);
  }
  return out;
}

function checkValue(path: string[], value: string): void {
  const group = path[0];
  const name = path.join(".");
  const ok =
    group === "color" ? HEX_COLOR.test(value) :
    group === "size" ? PX.test(value) :
    group === "font" ? SAFE_VALUE.test(value) :
    false;
  if (!ok) throw new Error(`bad value for ${name}: "${value}"`);
}

/** CSS text for the theme: one rule setting every token, scoped to the theme id. */
export function themeCss(theme: Theme): string {
  if (!NAME.test(theme.id)) throw new Error(`bad theme id "${theme.id}"`);
  return rule(`:root[data-theme="${theme.id}"]`, flattenTokens(theme.tokens));
}

function rule(selector: string, vars: Map<string, string>): string {
  const lines = [...vars].map(([k, v]) => `  ${k}: ${v};`);
  return `${selector} {\n${lines.join("\n")}\n}\n`;
}

/** WCAG 2.2 contrast ratio between two opaque #rrggbb colours ("contrast ratio", "relative luminance"). */
export function contrastRatio(hex_a: string, hex_b: string): number {
  const la = relativeLuminance(hex_a);
  const lb = relativeLuminance(hex_b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

/** WCAG 2.2 relative luminance of an opaque #rrggbb colour, 0 (black) to 1 (white). */
export function relativeLuminance(hex: string): number {
  const channel = (i: number) => {
    const c = parseInt(hex.slice(i, i + 2), 16) / 255;
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * channel(1) + 0.7152 * channel(3) + 0.0722 * channel(5);
}
