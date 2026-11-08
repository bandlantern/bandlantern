import { expect, test } from "vitest";
import type { SourceInfo } from "./api.generated";
import { sourceStatus } from "./sourceStatus";

const FILE: SourceInfo = { kind: "sigmf", path: "synth/tones_2m4.sigmf-meta", datatype: "cu8", rate_sps: 2_400_000, center_freq_hz: 1e8, loop: true };

test("demo, closed, connecting, idle, playing", () => {
  expect(sourceStatus(false, "ready", null)).toEqual({ lamp: "warn", label: "Demo signals" });
  expect(sourceStatus(true, "closed", null)).toEqual({ lamp: "fail", label: "Engine closed" });
  expect(sourceStatus(true, "authenticating", null).lamp).toBe("warn");
  expect(sourceStatus(true, "ready", null)).toEqual({ lamp: "idle", label: "No source" });
  expect(sourceStatus(true, "ready", FILE)).toEqual({ lamp: "ok", label: "tones_2m4.sigmf-meta · 2.4 MS/s" });
});
