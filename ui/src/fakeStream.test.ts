import { afterEach, beforeEach, expect, test, vi } from "vitest";
import { EngineClient, type ClientState } from "./client";
import type { InfoEvent } from "./api.generated";
import { FakeWebSocket } from "./fakeStream";

beforeEach(() => vi.useFakeTimers());
afterEach(() => vi.useRealTimers());

test("the fake engine says hello_ok, streams rows, and answers play/stop/set_fft with info", () => {
  const socket = new FakeWebSocket({ bin_count: 16, rows_per_s: 10, rate_sps: 2.4e6, center_freq_hz: 1e8 });
  const rows: number[] = [];
  const infos: InfoEvent[] = [];
  const states: ClientState[] = [];
  const client = new EngineClient(socket, "EXAMPLE_TOKEN", {
    onState: (s) => states.push(s),
    onRow: (r) => rows.push(r.bin_count),
    onInfo: (e) => infos.push(e),
  });
  vi.advanceTimersByTime(10);
  expect(states).toEqual(["authenticating", "ready"]);

  vi.advanceTimersByTime(350);
  expect(rows.length).toBeGreaterThanOrEqual(3);

  client.sendCommand("set_fft", { size: 256 });
  vi.advanceTimersByTime(250);
  expect(infos.at(-1)?.fft_size).toBe(256);
  expect(rows.at(-1)).toBe(256);

  client.sendCommand("stop");
  vi.advanceTimersByTime(10);
  expect(infos.at(-1)).toMatchObject({ streaming: false, source: null });
  const seen = rows.length;
  vi.advanceTimersByTime(500);
  expect(rows.length).toBe(seen);

  client.sendCommand("play", { path: "synth/a.sigmf-meta", loop: true });
  vi.advanceTimersByTime(10);
  expect(infos.at(-1)?.source?.path).toBe("synth/a.sigmf-meta");
  expect(infos.at(-1)?.streaming).toBe(true);
  client.close();
});
