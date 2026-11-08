// Spectrum trace (Canvas 2D): the averaged level per pixel column over a dB grid, on the same
// frequency view and dB range as the waterfall below it. Colors and fonts come from the theme's
// CSS variables, read at draw time so a theme switch needs no call.

import { averageInto, niceTicks, resampleMax } from "./spectrumMath";
import type { RowGeometry } from "./rowGeometry";
import { backingSize } from "./waterfallMath";

// A light average so the trace reads steadily at 30-60 rows/s but still follows a keyed carrier.
const AVERAGE_TIME_CONSTANT_S = 0.1;
const DB_GRID_MAX_LINES = 6;

export class Spectrum {
  /** True when render() would draw something new. */
  dirty = true;
  private ctx: CanvasRenderingContext2D;
  private average = new Float32Array(0); // dBFS per bin; reallocated when bin_count changes
  private geom: RowGeometry | null = null; // where the average's bins lie
  private view_lo_hz = 0;
  private view_hi_hz = 1;
  private columns = new Float32Array(0); // dBFS per device-pixel column
  private min_db = -110;
  private max_db = -20;
  private dpr = 1;

  /** grid: the dB lines and their labels (off for a strip too short to read them, the navigator). */
  constructor(
    private canvas: HTMLCanvasElement,
    private options = { grid: true },
  ) {
    this.ctx = canvas.getContext("2d")!;
  }

  /** Call when the CSS size or devicePixelRatio changes. */
  resize(css_w: number, css_h: number, dpr: number): void {
    const size = backingSize(css_w, css_h, dpr);
    this.dpr = dpr;
    if (this.canvas.width === size.w && this.canvas.height === size.h) return;
    this.canvas.width = size.w;
    this.canvas.height = size.h;
    this.columns = new Float32Array(size.w);
    this.render(); // resizing clears the canvas; redraw before the browser paints it blank
  }

  /**
   * One row and where its bins lie; dt_s since the previous row (0 or less: start over, e.g.
   * after a retune). A row laid out differently (another view) starts the average over too.
   */
  pushRow(bins_dbfs: Float32Array, geom: RowGeometry, dt_s: number): void {
    const g = this.geom;
    const same_layout =
      g !== null && g.bin_lo_hz === geom.bin_lo_hz && g.bin_width_hz === geom.bin_width_hz && g.lo_hz === geom.lo_hz && g.hi_hz === geom.hi_hz;
    if (bins_dbfs.length !== this.average.length || !same_layout || !(dt_s > 0)) {
      if (bins_dbfs.length !== this.average.length) this.average = new Float32Array(bins_dbfs.length);
      this.average.set(bins_dbfs);
    } else {
      averageInto(this.average, bins_dbfs, 1 - Math.exp(-dt_s / AVERAGE_TIME_CONSTANT_S));
    }
    this.geom = geom;
    this.dirty = true;
  }

  /** The frequency range the trace covers, Hz. */
  setView(lo_hz: number, hi_hz: number): void {
    this.view_lo_hz = lo_hz;
    this.view_hi_hz = hi_hz;
    this.dirty = true;
  }

  setRangeDb(min_db: number, max_db: number): void {
    this.min_db = min_db;
    this.max_db = max_db;
    this.dirty = true;
  }

  /** Averaged level at a frequency, or null outside the newest row or before the first row. */
  levelAt(freq_hz: number): number | null {
    const g = this.geom;
    if (!g || !(freq_hz >= g.lo_hz && freq_hz <= g.hi_hz)) return null;
    const v = this.average[Math.min(this.average.length - 1, Math.floor((freq_hz - g.bin_lo_hz) / g.bin_width_hz))];
    return Number.isFinite(v) ? v : null;
  }

  render(): void {
    this.dirty = false;
    const { ctx, canvas } = this;
    const w = canvas.width;
    const h = canvas.height;
    const style = getComputedStyle(canvas);
    const color = (name: string) => style.getPropertyValue(name).trim();
    ctx.fillStyle = color("--color-surface-sunken");
    ctx.fillRect(0, 0, w, h);
    const y_of = (db: number) => ((this.max_db - db) / (this.max_db - this.min_db)) * h;

    // dB grid with labels at the left edge.
    ctx.strokeStyle = color("--color-border");
    ctx.lineWidth = 1;
    ctx.fillStyle = color("--color-text-muted");
    ctx.font = `${parseFloat(color("--size-font-small")) * this.dpr}px ${color("--font-ui")}`;
    ctx.textBaseline = "bottom";
    for (const db of this.options.grid ? niceTicks(this.min_db, this.max_db, DB_GRID_MAX_LINES).values : []) {
      const y = Math.round(y_of(db)) + 0.5; // on a device pixel, so the line stays one pixel
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
      ctx.fillText(`${db} dB`, 4 * this.dpr, y - 2 * this.dpr);
    }

    if (!this.geom || this.average.length === 0 || w === 0) return;
    resampleMax(this.average, this.geom, this.view_lo_hz, this.view_hi_hz, this.columns);
    ctx.strokeStyle = color("--color-accent");
    ctx.lineWidth = Math.max(1, this.dpr);
    ctx.beginPath();
    let drawing = false;
    for (let x = 0; x < w; x++) {
      const db = this.columns[x];
      if (!Number.isFinite(db)) {
        drawing = false; // a gap, not a line through it
        continue;
      }
      const y = Math.min(h, Math.max(0, y_of(db)));
      if (drawing) ctx.lineTo(x + 0.5, y);
      else ctx.moveTo(x + 0.5, y);
      drawing = true;
    }
    ctx.stroke();
  }
}
