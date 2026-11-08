import { useEffect, useRef, useState } from "preact/hooks";
import { AutoRange } from "../autoRange";
import { Panel } from "../widgets/Panel";
import type { RowBus } from "../rowBus";
import { Spectrum } from "../spectrum";
import { formatTickMHz, niceTicks } from "../spectrumMath";
import { Waterfall } from "../waterfall";
import type { ColorStops } from "../waterfallMath";

interface Band {
  lo_hz: number;
  hi_hz: number;
}

// About one frequency label per this many CSS pixels.
const AXIS_LABEL_SPACING_PX = 90;

/**
 * The Spectrum trace panel over the Waterfall panel, fed rows through a RowBus. Both show the
 * whole band of the newest row (frames.md: each row says its own centre and rate) and share one
 * automatic dB range. Display only; the canvases are not interactive.
 */
export function LiveWaterfall({ rows, colormap }: { rows: RowBus; colormap: ColorStops }) {
  const canvas_ref = useRef<HTMLCanvasElement>(null);
  const spectrum_canvas_ref = useRef<HTMLCanvasElement>(null);
  const waterfall_ref = useRef<Waterfall | null>(null);
  const [width_px, setWidthPx] = useState(0);
  const [band, setBand] = useState<Band | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const canvas = canvas_ref.current!;
    const spectrum_canvas = spectrum_canvas_ref.current!;
    let waterfall: Waterfall;
    try {
      waterfall = new Waterfall(canvas);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      return;
    }
    const spectrum = new Spectrum(spectrum_canvas);
    waterfall_ref.current = waterfall;
    const auto = new AutoRange();

    const resize = () => {
      const rect = canvas.getBoundingClientRect();
      const spectrum_rect = spectrum_canvas.getBoundingClientRect();
      waterfall.resize(rect.width, rect.height, window.devicePixelRatio);
      spectrum.resize(spectrum_rect.width, spectrum_rect.height, window.devicePixelRatio);
      setWidthPx(rect.width);
    };
    const observer = new ResizeObserver(resize);
    observer.observe(canvas);
    observer.observe(spectrum_canvas);
    window.addEventListener("resize", resize); // devicePixelRatio changes (zoom) fire this too
    resize();

    let last_sample_index = 0;
    let shown: Band | null = null;
    const unsubscribe = rows.subscribe((row, geom) => {
      const range = auto.update(row);
      if (range) {
        waterfall.setRangeDb(range.min_db, range.max_db);
        spectrum.setRangeDb(range.min_db, range.max_db);
      }
      // frames.md: use the row's own centre and rate, not the last `info`.
      if (!shown || shown.lo_hz !== geom.lo_hz || shown.hi_hz !== geom.hi_hz) {
        shown = { lo_hz: geom.lo_hz, hi_hz: geom.hi_hz };
        waterfall.setView(geom.lo_hz, geom.hi_hz);
        spectrum.setView(geom.lo_hz, geom.hi_hz);
        setBand(shown);
      }
      waterfall.pushRow(row.bins_dbfs, geom);
      spectrum.pushRow(row.bins_dbfs, geom, (row.sample_index - last_sample_index) / row.rate_sps);
      last_sample_index = row.sample_index;
    });

    let raf_id = 0;
    const frame = () => {
      if (waterfall.dirty) waterfall.render();
      if (spectrum.dirty) spectrum.render();
      raf_id = requestAnimationFrame(frame);
    };
    raf_id = requestAnimationFrame(frame);

    return () => {
      cancelAnimationFrame(raf_id);
      observer.disconnect();
      window.removeEventListener("resize", resize);
      unsubscribe();
      waterfall_ref.current = null;
    };
  }, [rows]);

  useEffect(() => {
    waterfall_ref.current?.setColormap(colormap);
  }, [colormap]);

  const ticks = band && width_px > 0 ? niceTicks(band.lo_hz, band.hi_hz, Math.max(2, Math.floor(width_px / AXIS_LABEL_SPACING_PX))) : null;

  return (
    <div class="display">
      <Panel title="Spectrum" class="spectrum-panel">
        <canvas ref={spectrum_canvas_ref} class="spectrum-canvas" role="img" aria-label="Spectrum trace: level per frequency" />
      </Panel>
      <Panel title="Waterfall" class="waterfall-panel">
        <div class="freq-axis" aria-hidden="true">
          {band &&
            ticks?.values.map((hz) => (
              <span class="freq-tick" style={{ left: `${((hz - band.lo_hz) / (band.hi_hz - band.lo_hz)) * 100}%` }}>
                {formatTickMHz(hz, ticks.step)}
              </span>
            ))}
        </div>
        {error && <p class="notice" role="alert">The waterfall needs WebGL2: {error}</p>}
        <canvas ref={canvas_ref} class="waterfall-canvas" role="img" aria-label="Waterfall: spectrum over time, newest at the top" />
      </Panel>
    </div>
  );
}
