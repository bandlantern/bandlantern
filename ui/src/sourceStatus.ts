// The Status panel's headline: the source and its state in a few words, and a lamp colour.

import type { SourceInfo } from "./api.generated";
import type { ClientState } from "./client";

export type Lamp = "ok" | "warn" | "fail" | "idle";

export interface SourceStatus {
  lamp: Lamp;
  label: string;
}

export function sourceStatus(live: boolean, state: ClientState, source: SourceInfo | null): SourceStatus {
  if (!live) return { lamp: "warn", label: "Demo signals" };
  if (state === "closed") return { lamp: "fail", label: "Engine closed" };
  if (state !== "ready") return { lamp: "warn", label: "Connecting…" };
  if (!source) return { lamp: "idle", label: "No source" };
  const name = source.path.split("/").pop() ?? source.path;
  return { lamp: "ok", label: `${name} · ${source.rate_sps / 1e6} MS/s` };
}
