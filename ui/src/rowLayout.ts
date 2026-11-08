// Per-row frequency layout of the waterfall's history (DOM-free). Rows keep the frequency range
// they were made for: an old full-band row and a newer zoom row share the screen, and the shader
// looks each up by frequency. The GPU gets f32 offsets from a reference frequency near the
// view: f32 has 24 bits of mantissa, so Hz at 1.3 GHz would be ~128 Hz apart, but an offset
// within a few MHz is below 1 Hz. The f64 originals stay here, so a re-base loses nothing.

/** Re-base when the view's centre is this far from the reference (offsets stay within a few MHz). */
export const REBASE_DISTANCE_HZ = 2_000_000;

export class RowLayouts {
  /** Per ring row: lower and upper edge of the shown part of the row, Hz relative to ref_hz (f32). */
  readonly offsets: Float32Array;
  ref_hz = 0;
  private edges_hz: Float64Array; // the same, absolute

  constructor(readonly height: number) {
    // 0, 0: an empty range, so a row never written shows the empty colour.
    this.offsets = new Float32Array(height * 2);
    this.edges_hz = new Float64Array(height * 2);
  }

  set(ring_row: number, lo_hz: number, hi_hz: number): void {
    this.edges_hz[ring_row * 2] = lo_hz;
    this.edges_hz[ring_row * 2 + 1] = hi_hz;
    this.offsets[ring_row * 2] = lo_hz - this.ref_hz;
    this.offsets[ring_row * 2 + 1] = hi_hz - this.ref_hz;
  }

  /** True when center_hz is far enough from the reference that the GPU offsets lose precision. */
  needsRebase(center_hz: number): boolean {
    return Math.abs(center_hz - this.ref_hz) > REBASE_DISTANCE_HZ;
  }

  /** Move the reference; recomputes every offset from the f64 edges (rows never written stay empty). */
  rebase(ref_hz: number): void {
    this.ref_hz = ref_hz;
    for (let i = 0; i < this.height; i++) {
      const lo_hz = this.edges_hz[i * 2];
      const hi_hz = this.edges_hz[i * 2 + 1];
      const written = hi_hz > lo_hz;
      this.offsets[i * 2] = written ? lo_hz - ref_hz : 0;
      this.offsets[i * 2 + 1] = written ? hi_hz - ref_hz : 0;
    }
  }
}
