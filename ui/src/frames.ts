// Binary frame reader. Contract: docs/api/frames.md ("Header", "Reader rules").
// All fields little-endian; payload is found by seeking to header_len, not by assuming 48.

export const FRAME_MAGIC = [0x42, 0x4c, 0x46, 0x00] as const; // "BLF\0"
export const FRAME_HEADER_LEN = 48; // frames.md v1; a minor version may append fields (header_len grows)
export const KIND_SPECTRUM_ROW = 1;
export const PAYLOAD_F32_DBFS = 1;

export interface SpectrumRow {
  seq: number; // u64 on the wire; exact up to 2^53
  sample_index: number; // u64 on the wire; exact up to 2^53
  center_freq_hz: number;
  rate_sps: number;
  bin_count: number;
  /** f32 dBFS, FFT-shifted (lowest frequency first, DC at bin_count / 2). */
  bins_dbfs: Float32Array;
}

export type FrameResult =
  | { status: "row"; row: SpectrumRow }
  | { status: "ignored"; reason: string } // valid envelope, unknown kind / payload_format
  | { status: "rejected"; reason: string }; // malformed: drop the frame, log once

export function parseFrame(buffer: ArrayBuffer): FrameResult {
  if (buffer.byteLength < FRAME_HEADER_LEN) {
    return { status: "rejected", reason: "short frame" };
  }
  const view = new DataView(buffer);
  for (let i = 0; i < FRAME_MAGIC.length; i++) {
    if (view.getUint8(i) !== FRAME_MAGIC[i]) {
      return { status: "rejected", reason: "bad magic" };
    }
  }
  if (view.getUint16(4, true) !== 1) {
    return { status: "rejected", reason: "unsupported version" };
  }
  const header_len = view.getUint16(8, true);
  if (header_len < FRAME_HEADER_LEN) {
    return { status: "rejected", reason: "header_len < 48" };
  }
  if (view.getUint16(6, true) !== KIND_SPECTRUM_ROW) {
    return { status: "ignored", reason: "unknown kind" };
  }
  if (view.getUint16(10, true) !== PAYLOAD_F32_DBFS) {
    return { status: "ignored", reason: "unknown payload_format" };
  }
  const bin_count = view.getUint32(12, true);
  if (buffer.byteLength !== header_len + bin_count * 4) {
    return { status: "rejected", reason: "length mismatch" };
  }
  const bins_dbfs = new Float32Array(bin_count);
  for (let i = 0; i < bin_count; i++) {
    bins_dbfs[i] = view.getFloat32(header_len + i * 4, true);
  }
  return {
    status: "row",
    row: {
      seq: Number(view.getBigUint64(16, true)),
      sample_index: Number(view.getBigUint64(24, true)),
      center_freq_hz: view.getFloat64(32, true),
      rate_sps: view.getFloat64(40, true),
      bin_count,
      bins_dbfs,
    },
  };
}
