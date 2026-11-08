// Hands each spectrum row, with where its bins lie, to whichever waterfall is mounted.

import type { SpectrumRow } from "./frames";
import { rowGeometry, type RowGeometry } from "./rowGeometry";

export class RowBus {
  private listener: ((row: SpectrumRow, geom: RowGeometry) => void) | null = null;

  push(row: SpectrumRow): void {
    this.listener?.(row, rowGeometry(row));
  }

  /** One listener at a time; the returned function removes it. */
  subscribe(listener: (row: SpectrumRow, geom: RowGeometry) => void): () => void {
    this.listener = listener;
    return () => {
      if (this.listener === listener) this.listener = null;
    };
  }
}
