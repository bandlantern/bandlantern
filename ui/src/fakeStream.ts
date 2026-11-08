// Fake engine for running the whole client path in a browser with no engine.
// Encoder layout: docs/api/frames.md "Header". Not used in production.

import type { WebSocketLike } from "./client";
import { FRAME_HEADER_LEN, KIND_SPECTRUM_ROW, PAYLOAD_F32_DBFS, type SpectrumRow } from "./frames";

/** Serialize one spectrum frame. Header fields come from `row`; payload from `row.bins_dbfs`. */
export function encodeSpectrumFrame(row: SpectrumRow): ArrayBuffer {
  const buffer = new ArrayBuffer(FRAME_HEADER_LEN + row.bin_count * 4);
  const view = new DataView(buffer);
  view.setUint8(0, 0x42);
  view.setUint8(1, 0x4c);
  view.setUint8(2, 0x46);
  view.setUint8(3, 0x00);
  view.setUint16(4, 1, true); // version
  view.setUint16(6, KIND_SPECTRUM_ROW, true);
  view.setUint16(8, FRAME_HEADER_LEN, true);
  view.setUint16(10, PAYLOAD_F32_DBFS, true);
  view.setUint32(12, row.bin_count, true);
  view.setBigUint64(16, BigInt(row.seq), true);
  view.setBigUint64(24, BigInt(row.sample_index), true);
  view.setFloat64(32, row.center_freq_hz, true);
  view.setFloat64(40, row.rate_sps, true);
  for (let i = 0; i < row.bin_count; i++) {
    view.setFloat32(FRAME_HEADER_LEN + i * 4, row.bins_dbfs[i], true);
  }
  return buffer;
}

export interface FakeStreamConfig {
  bin_count: number;
  rows_per_s: number;
  rate_sps: number;
  center_freq_hz: number;
}

export const DEFAULT_FAKE_CONFIG: FakeStreamConfig = {
  bin_count: 2048,
  rows_per_s: 30,
  rate_sps: 2.4e6,
  center_freq_hz: 100e6,
};

const NOISE_FLOOR_DBFS = -95;
const TONE_HALF_WIDTH_BINS = 4;
// Fixed tones: offset from centre as a fraction of the sample rate, and peak level.
const TONES = [
  { offset_frac: -0.31, level_dbfs: -25 },
  { offset_frac: -0.12, level_dbfs: -40 },
  { offset_frac: 0.07, level_dbfs: -30 },
  { offset_frac: 0.28, level_dbfs: -50 },
];
const SWEEP_LEVEL_DBFS = -35;
const SWEEP_PERIOD_S = 8;

/** Generates rows; reuses one payload array, so only the encoded ArrayBuffer is allocated per row. */
export class FakeRowSource {
  private seq = 0;
  private rng = 12345;
  private bins: Float32Array;

  constructor(private config: FakeStreamConfig = DEFAULT_FAKE_CONFIG) {
    this.bins = new Float32Array(config.bin_count);
  }

  nextFrame(): ArrayBuffer {
    const c = this.config;
    const n = c.bin_count;
    const t_s = this.seq / c.rows_per_s;
    for (let i = 0; i < n; i++) {
      this.bins[i] = NOISE_FLOOR_DBFS + (this.random() - 0.5) * 6;
    }
    for (const tone of TONES) {
      this.addTone(n / 2 + tone.offset_frac * n, tone.level_dbfs);
    }
    const sweep_pos = (t_s % SWEEP_PERIOD_S) / SWEEP_PERIOD_S; // 0..1 across the span
    this.addTone(sweep_pos * (n - 1), SWEEP_LEVEL_DBFS);

    const row: SpectrumRow = {
      seq: this.seq,
      sample_index: this.seq * Math.floor(c.rate_sps / c.rows_per_s),
      center_freq_hz: c.center_freq_hz,
      rate_sps: c.rate_sps,
      bin_count: n,
      bins_dbfs: this.bins,
    };
    this.seq++;
    return encodeSpectrumFrame(row);
  }

  // Parabolic main lobe in dB, 12 dB down per 1.5 bins; keeps the stronger of tone and floor.
  private addTone(centre_bin: number, level_dbfs: number): void {
    const first = Math.max(0, Math.floor(centre_bin) - TONE_HALF_WIDTH_BINS);
    const last = Math.min(this.bins.length - 1, Math.ceil(centre_bin) + TONE_HALF_WIDTH_BINS);
    for (let i = first; i <= last; i++) {
      const d = (i - centre_bin) / 1.5;
      const db = level_dbfs - 12 * d * d;
      if (db > this.bins[i]) {
        this.bins[i] = db;
      }
    }
  }

  // Small LCG so the stream is deterministic.
  private random(): number {
    this.rng = (Math.imul(this.rng, 1664525) + 1013904223) >>> 0;
    return this.rng / 4294967296;
  }
}

/**
 * Plays the engine's side so the whole client path runs in a browser with no engine: after a
 * `hello` it replies hello_ok and streams synthetic rows on a timer; get_info, play, stop and
 * set_fft are answered with `info` the way messages.md says (play pretends to start any path).
 */
export class FakeWebSocket implements WebSocketLike {
  binaryType = "blob";
  onopen: ((ev: unknown) => void) | null = null;
  onmessage: ((ev: { data: unknown }) => void) | null = null;
  onclose: ((ev: { code: number; reason: string }) => void) | null = null;

  private timer: ReturnType<typeof setInterval> | null = null;
  private source: FakeRowSource;
  private streaming = false;
  private path = "fake/synthetic.sigmf-meta";
  private loop = true;

  constructor(private config: FakeStreamConfig = DEFAULT_FAKE_CONFIG) {
    this.source = new FakeRowSource(config);
    setTimeout(() => this.onopen?.({}), 0);
  }

  send(data: string): void {
    const msg = JSON.parse(data);
    switch (msg.type) {
      case "hello":
        this.reply({ type: "hello_ok", re: msg.id, api_version: "0.1", engine_version: "fake" });
        this.start();
        break;
      case "get_info":
        this.replyInfo(msg.id);
        break;
      case "play":
        this.path = String(msg.path);
        this.loop = msg.loop === true;
        this.start();
        this.replyInfo(msg.id);
        break;
      case "stop":
        this.halt();
        this.replyInfo(msg.id);
        break;
      case "set_fft":
        this.config = { ...this.config, bin_count: msg.size ?? this.config.bin_count, rows_per_s: msg.rows_per_s ?? this.config.rows_per_s };
        if (this.streaming) this.start();
        this.replyInfo(msg.id);
        break;
    }
  }

  close(code = 1000, reason = ""): void {
    this.halt();
    this.onclose?.({ code, reason });
  }

  private reply(fields: object): void {
    setTimeout(() => this.onmessage?.({ data: JSON.stringify({ v: 1, ...fields }) }), 0);
  }

  private replyInfo(re: number): void {
    this.reply({
      type: "info",
      re,
      streaming: this.streaming,
      fft_size: this.config.bin_count,
      rows_per_s: this.config.rows_per_s,
      source: this.streaming
        ? {
            kind: "sigmf",
            path: this.path,
            datatype: "cu8",
            rate_sps: this.config.rate_sps,
            center_freq_hz: this.config.center_freq_hz,
            loop: this.loop,
          }
        : null,
    });
  }

  // (Re)starts the row timer with the current settings, like the engine restarting its pipeline.
  private start(): void {
    this.halt();
    this.source = new FakeRowSource(this.config);
    this.streaming = true;
    this.timer = setInterval(() => this.onmessage?.({ data: this.source.nextFrame() }), 1000 / this.config.rows_per_s);
  }

  private halt(): void {
    if (this.timer !== null) {
      clearInterval(this.timer);
      this.timer = null;
    }
    this.streaming = false;
  }
}
