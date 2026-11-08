import { ledCells } from "./ledLogic";

// Original seven-segment geometry in a 12 x 20 cell: hexagonal bars with 0.4-unit gaps where
// they meet, decimal point to the lower right. Rendered slanted by the stylesheet.
const H = (y: number) => `1.4,${y + 1} 2.4,${y} 9.6,${y} 10.6,${y + 1} 9.6,${y + 2} 2.4,${y + 2}`;
const V = (x: number, y0: number, y1: number) =>
  `${x + 1},${y0 + 0.4} ${x + 2},${y0 + 1.4} ${x + 2},${y1 - 1.4} ${x + 1},${y1 - 0.4} ${x},${y1 - 1.4} ${x},${y0 + 1.4}`;
const SEGMENT_POINTS: Record<string, string> = {
  a: H(0),
  b: V(10, 1, 10),
  c: V(10, 10, 19),
  d: H(18),
  e: V(0, 10, 19),
  f: V(0, 1, 10),
  g: H(9),
};

function Digit({ segments, dp }: { segments: string; dp: boolean }) {
  return (
    <svg class="led-digit" viewBox="-1 -0.5 16 21" aria-hidden="true">
      {Object.entries(SEGMENT_POINTS).map(([seg, points]) => (
        <polygon points={points} class={segments.includes(seg) ? "on" : "off"} />
      ))}
      <circle cx="13.2" cy="19" r="1.1" class={dp ? "on" : "off"} />
    </svg>
  );
}

/** A labelled LED readout. `text` null = no value yet (blank cells, "unknown" for screen readers). */
export function LedReadout({ label, text, unit, width }: { label: string; text: string | null; unit?: string; width: number }) {
  const spoken = text === null ? "unknown" : `${text}${unit ? " " + unit : ""}`;
  return (
    <div class="readout">
      <span>{label}</span>
      <div class="readout-row">
        <div class="led" role="img" aria-label={`${label}: ${spoken}`}>
          {ledCells(text ?? "", width).map((cell) => (
            <Digit {...cell} />
          ))}
        </div>
        <span class="readout-unit" aria-hidden="true">{unit}</span>
      </div>
    </div>
  );
}
