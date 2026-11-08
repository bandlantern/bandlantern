import { expect, test } from "vitest";
import { EngineClient, type WebSocketLike } from "./client";
import { FakeRowSource } from "./fakeStream";
import { encodeSpectrumFrame } from "./fakeStream";
import type { SpectrumRow } from "./frames";

class TestSocket implements WebSocketLike {
  binaryType = "blob";
  onopen: ((ev: unknown) => void) | null = null;
  onmessage: ((ev: { data: unknown }) => void) | null = null;
  onclose: ((ev: { code: number; reason: string }) => void) | null = null;
  sent: string[] = [];
  send(data: string): void {
    this.sent.push(data);
  }
  close(): void {}
  deliver(data: unknown): void {
    this.onmessage?.({ data });
  }
}

const HELLO_OK = JSON.stringify({ v: 1, type: "hello_ok", re: 1, api_version: "0.1", engine_version: "t" });

function frame(seq: number): ArrayBuffer {
  const row: SpectrumRow = {
    seq,
    sample_index: 0,
    center_freq_hz: 1e8,
    rate_sps: 2.4e6,
   
    bin_count: 2,
    bins_dbfs: new Float32Array([-1, -2]),
  };
  return encodeSpectrumFrame(row);
}

function setup() {
  const socket = new TestSocket();
  const rows: number[] = [];
  const client = new EngineClient(socket, "tok", { onRow: (r) => rows.push(r.seq) });
  return { socket, rows, client };
}

test("hello is the first message sent, with the token", () => {
  const { socket, client } = setup();
  expect(socket.sent).toEqual([]);
  socket.onopen?.({});
  expect(socket.sent).toHaveLength(1);
  expect(JSON.parse(socket.sent[0])).toEqual({ v: 1, type: "hello", id: 1, token: "tok" });
  expect(client.state).toBe("authenticating");
  expect(socket.binaryType).toBe("arraybuffer");
});

test("nothing is processed before hello_ok", () => {
  const { socket, rows, client } = setup();
  socket.onopen?.({});
  socket.deliver(frame(0));
  socket.deliver(JSON.stringify({ v: 1, type: "drop", rows_dropped: 5 }));
  expect(rows).toEqual([]);
  expect(client.engineReportedDropped).toBe(0);
  expect(client.sendCommand("get_info")).toBeNull();

  socket.deliver(HELLO_OK);
  expect(client.state).toBe("ready");
  socket.deliver(frame(0));
  expect(rows).toEqual([0]);
  expect(client.sendCommand("get_info")).toBe(2);
});

test("seq gaps are counted as dropped rows", () => {
  const { socket, rows, client } = setup();
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  socket.deliver(frame(0));
  socket.deliver(frame(1));
  expect(client.seqGapRows).toBe(0);
  socket.deliver(frame(5)); // 2,3,4 missing
  expect(client.seqGapRows).toBe(3);
  socket.deliver(frame(6));
  expect(client.seqGapRows).toBe(3);
  expect(rows).toEqual([0, 1, 5, 6]);
});

test("a first row with seq > 0 counts the missing leading rows", () => {
  const { socket, client } = setup();
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  socket.deliver(frame(2));
  expect(client.seqGapRows).toBe(2);
});

test("drop events accumulate; malformed frames are logged once and skipped", () => {
  const socket = new TestSocket();
  const logs: string[] = [];
  const rows: number[] = [];
  const client = new EngineClient(socket, "t", { onRow: (r) => rows.push(r.seq), onLog: (m) => logs.push(m) });
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  socket.deliver(JSON.stringify({ v: 1, type: "drop", rows_dropped: 12 }));
  socket.deliver(JSON.stringify({ v: 1, type: "drop", rows_dropped: 3 }));
  expect(client.engineReportedDropped).toBe(15);
  socket.deliver(new ArrayBuffer(10));
  socket.deliver(new ArrayBuffer(10));
  expect(logs).toHaveLength(1);
  expect(rows).toEqual([]);
});

test("fake row source emits parseable, sequential frames", () => {
  const source = new FakeRowSource({ bin_count: 64, rows_per_s: 30, rate_sps: 2.4e6, center_freq_hz: 1e8 });
  const { socket, rows } = setup();
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  for (let i = 0; i < 3; i++) {
    socket.deliver(source.nextFrame());
  }
  expect(rows).toEqual([0, 1, 2]);
});

test("an error event passes the id of the command it answers", () => {
  const socket = new TestSocket();
  const errors: Array<[string, number | undefined]> = [];
  new EngineClient(socket, "tok", { onError: (code, _message, re) => errors.push([code, re]) });
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  socket.deliver(JSON.stringify({ v: 1, type: "error", re: 5, code: "bad_value", message: "span_hz" }));
  socket.deliver(JSON.stringify({ v: 1, type: "error", code: "source_lost", message: "gone" }));
  expect(errors).toEqual([
    ["bad_value", 5],
    ["source_lost", undefined],
  ]);
});

test("commands get the envelope; their fields are checked against the schema types", () => {
  const { socket, client } = setup();
  socket.onopen?.({});
  socket.deliver(HELLO_OK);

  expect(client.sendCommand("play", { path: "synth/tones.sigmf-meta", loop: true })).toBe(2);
  expect(JSON.parse(socket.sent[1])).toEqual({ v: 1, type: "play", id: 2, path: "synth/tones.sigmf-meta", loop: true });
  expect(client.sendCommand("stop")).toBe(3);
  expect(JSON.parse(socket.sent[2])).toEqual({ v: 1, type: "stop", id: 3 });
  expect(client.sendCommand("set_fft", { size: 4096, rows_per_s: 20 })).toBe(4);

  // Compile-time checks (tsc in `npm run build`); each line is still sent at run time.
  // @ts-expect-error play needs a path
  client.sendCommand("play");
  // @ts-expect-error the client sends hello itself
  client.sendCommand("hello", { token: "x" });
  // @ts-expect-error not a command
  client.sendCommand("dev_clock");
});

test("drop events reach their handler", () => {
  const socket = new TestSocket();
  const drops: number[] = [];
  new EngineClient(socket, "tok", { onDrop: (n) => drops.push(n) });
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  socket.deliver(JSON.stringify({ v: 1, type: "drop", rows_dropped: 4 }));
  expect(drops).toEqual([4]);
});

test("info events reach their handler with re", () => {
  const socket = new TestSocket();
  const seen: Array<[number | undefined, boolean]> = [];
  new EngineClient(socket, "tok", { onInfo: (e) => seen.push([e.re, e.streaming]) });
  socket.onopen?.({});
  socket.deliver(HELLO_OK);
  socket.deliver(JSON.stringify({ v: 1, type: "info", re: 2, streaming: false, fft_size: 2048, rows_per_s: 30, source: null }));
  expect(seen).toEqual([[2, false]]);
});
