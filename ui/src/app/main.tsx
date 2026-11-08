// The A0 bench UI: Spectrum and Waterfall panels fed by the engine's binary frames, a Source
// panel (play / stop / FFT settings) and a Status panel (LED readouts). Opened from the engine's
// link (#token=...) it drives the real engine; with ?fake it runs against an in-page fake engine
// of synthetic rows, so the waterfall can be seen without the engine.
//   ?fake                                  synthetic rows, no engine
//   ?theme=graphite|teal-navy|slate        starting theme (default graphite)
//   ?colormap=spectrum|...                 starting colormap (default spectrum)
//   ?engine=127.0.0.1:8780                 connect straight to this engine (dev server; ui/README.md)
import { render } from "preact";
import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import graphite from "../../themes/graphite.json";
import slate from "../../themes/slate.json";
import tealNavy from "../../themes/teal-navy.json";
import type { InfoEvent } from "../api.generated";
import { EngineClient, type ClientState, type WebSocketLike } from "../client";
import { COLORMAPS } from "../colormaps";
import { engineUrl } from "../connect";
import { FakeWebSocket } from "../fakeStream";
import { RowBus } from "../rowBus";
import { sourceStatus } from "../sourceStatus";
import { bootstrapToken, type StorageLike } from "../token";
import { themeCss, type Theme } from "../tokens";
import { Check } from "../widgets/Check";
import { LedReadout } from "../widgets/LedReadout";
import { Panel } from "../widgets/Panel";
import "../widgets/widgets.css";
import "./app.css";
import { LiveWaterfall } from "./LiveWaterfall";

const THEMES = [graphite, tealNavy, slate] as Theme[];
const FFT_SIZES = [256, 512, 1024, 2048, 4096, 8192, 16384]; // messages.md set_fft: power of two, 256 ... 16384
const DEFAULT_FFT_SIZE = 2048;
const DEFAULT_ROWS_PER_S = 30;

// Before anything else runs (README "Bootstrap in a plain browser" step 3): take the token out
// of the fragment, keep it for this tab, and clear the fragment from the address bar.
const query = new URLSearchParams(location.search);
const token_state = bootstrapToken(location.hash, location.pathname + location.search, safeStorage(), history);
const fake = query.has("fake");

/** sessionStorage can throw (blocked site data); then the token lives only until reload. */
function safeStorage(): StorageLike {
  try {
    sessionStorage.getItem("probe");
    return sessionStorage;
  } catch {
    const data = new Map<string, string>();
    return { getItem: (k) => data.get(k) ?? null, setItem: (k, v) => void data.set(k, v) };
  }
}

interface ReplyError {
  command: string;
  code: string;
  message: string;
}

/** What the engine said about a failed command, in words a person can act on. */
function describeError(e: ReplyError): { headline: string; detail: string } {
  if (e.code === "not_available") {
    return {
      headline: `The engine cannot ${e.command} yet: this build has no file playback.`,
      detail: `${e.code}: ${e.message}`,
    };
  }
  return { headline: `${e.command} failed (${e.code}).`, detail: e.message };
}

function App({ token }: { token: string | null }) {
  const rows = useMemo(() => new RowBus(), []);
  const client_ref = useRef<EngineClient | null>(null);
  const pending = useRef(new Map<number, string>()); // command id -> command type, to name an error's command
  const [state, setState] = useState<ClientState>("connecting");
  const [closed_why, setClosedWhy] = useState<string | null>(null);
  const [info, setInfo] = useState<InfoEvent | null>(null);
  const [live, setLive] = useState<{ center_freq_hz: number; rate_sps: number } | null>(null);
  const [dropped, setDropped] = useState(0);
  const [reply_error, setReplyError] = useState<ReplyError | null>(null);

  const [theme_id, setThemeId] = useState((THEMES.find((t) => t.id === query.get("theme")) ?? THEMES[0]).id);
  const [colormap_id, setColormapId] = useState((COLORMAPS.find((c) => c.id === query.get("colormap")) ?? COLORMAPS[0]).id);
  const [path, setPath] = useState("");
  const [loop, setLoop] = useState(true);
  const [fft_size, setFftSize] = useState(DEFAULT_FFT_SIZE);
  const [rows_per_s, setRowsPerS] = useState(DEFAULT_ROWS_PER_S);

  useEffect(() => {
    document.documentElement.dataset.theme = theme_id;
  }, [theme_id]);

  useEffect(() => {
    const socket: WebSocketLike = fake ? new FakeWebSocket() : (new WebSocket(engineUrl(location.host, query.get("engine"))) as WebSocketLike);
    let last_band: { center_freq_hz: number; rate_sps: number } | null = null;
    const client: EngineClient = new EngineClient(socket, token ?? "", {
      onState: (s) => {
        setState(s);
        if (s === "ready") send("get_info");
        if (s === "closed") {
          // README "Close codes": 1008 "auth" = the token was refused.
          setClosedWhy(client.closeCode === 1008 ? "The engine refused the token. Open the link from the engine again." : "The connection to the engine closed.");
        }
      },
      onRow: (row) => {
        rows.push(row);
        // Centre and rate come from the rows (frames.md), shown when they change.
        if (row.center_freq_hz !== last_band?.center_freq_hz || row.rate_sps !== last_band?.rate_sps) {
          last_band = { center_freq_hz: row.center_freq_hz, rate_sps: row.rate_sps };
          setLive(last_band);
        }
      },
      onInfo: (e) => {
        setInfo(e);
        setFftSize(e.fft_size);
        setRowsPerS(e.rows_per_s);
        if (e.re !== undefined && pending.current.get(e.re) === "play") setReplyError(null);
        if (!e.streaming) setLive(null);
      },
      onDrop: () => setDropped(client.engineReportedDropped),
      onError: (code, message, re) => {
        const command = (re !== undefined && pending.current.get(re)) || "request";
        setReplyError({ command, code, message });
      },
      onLog: (m) => console.warn("bandlantern:", m),
    });
    const send = (type: "get_info") => {
      const id = client.sendCommand(type);
      if (id !== null) pending.current.set(id, type);
    };
    client_ref.current = client;
    return () => {
      client_ref.current = null;
      client.close();
    };
  }, [rows]);

  /** Send a command and remember its id, so an error reply can name it. */
  const run = (type: string, send: (client: EngineClient) => number | null) => {
    const client = client_ref.current;
    if (!client) return;
    setReplyError(null);
    const id = send(client);
    if (id !== null) pending.current.set(id, type);
  };

  const ready = state === "ready";
  const status = sourceStatus(fake || token !== null, state, info?.source ?? null);
  const colormap = COLORMAPS.find((c) => c.id === colormap_id) ?? COLORMAPS[0];
  const source = info?.source ?? null;
  const center_hz = live?.center_freq_hz ?? (info?.streaming ? source?.center_freq_hz : undefined);
  const rate_sps = live?.rate_sps ?? (info?.streaming ? source?.rate_sps : undefined);
  const shown_error = reply_error ? describeError(reply_error) : null;

  return (
    <div class="app">
      <header class="appbar">
        <span class="brand">Bandlantern</span>
        <span class="spacer" />
        <label class="field">
          Theme
          <select class="input" value={theme_id} onChange={(e) => setThemeId(e.currentTarget.value)}>
            {THEMES.map((t) => (
              <option value={t.id}>{t.name}</option>
            ))}
          </select>
        </label>
        <label class="field">
          Colormap
          <select class="input" value={colormap_id} onChange={(e) => setColormapId(e.currentTarget.value)}>
            {COLORMAPS.map((c) => (
              <option value={c.id}>{c.name}</option>
            ))}
          </select>
        </label>
      </header>
      <main class="workspace">
        <LiveWaterfall rows={rows} colormap={colormap.stops} />
        <div class="sidebar">
          <Panel title="Source">
            <form
              class="form-row"
              onSubmit={(e) => {
                e.preventDefault();
                if (path.trim() !== "") run("play", (c) => c.sendCommand("play", { path: path.trim(), loop }));
              }}
            >
              <label class="field wide">
                File (SigMF meta or WAV, under the engine's data root)
                <input class="input" type="text" value={path} placeholder="synth/tones_2m4.sigmf-meta" spellcheck={false} onInput={(e) => setPath(e.currentTarget.value)} />
              </label>
              <Check label="Loop" checked={loop} onChange={setLoop} />
              <span class="spacer" />
              <button type="submit" class="button primary" disabled={!ready || path.trim() === ""}>
                Play
              </button>
              <button type="button" class="button" disabled={!ready} onClick={() => run("stop", (c) => c.sendCommand("stop"))}>
                Stop
              </button>
            </form>
            <form
              class="form-row"
              onSubmit={(e) => {
                e.preventDefault();
                run("set_fft", (c) => c.sendCommand("set_fft", { size: fft_size, rows_per_s }));
              }}
            >
              <label class="field">
                FFT size
                <select class="input" value={fft_size} onChange={(e) => setFftSize(Number(e.currentTarget.value))}>
                  {FFT_SIZES.map((n) => (
                    <option value={n}>{n}</option>
                  ))}
                </select>
              </label>
              <label class="field">
                Rows/s
                <input class="input" type="number" min={1} max={60} step={1} value={rows_per_s} onInput={(e) => setRowsPerS(Number(e.currentTarget.value))} />
              </label>
              <button type="submit" class="button" disabled={!ready}>
                Apply
              </button>
            </form>
            <div aria-live="polite">
              {shown_error && (
                <p class="notice" role="alert">
                  {shown_error.headline}
                  <small>{shown_error.detail}</small>
                </p>
              )}
            </div>
          </Panel>
          <Panel title="Status">
            <div class="status-head">
              <span class={`status-dot ${status.lamp}`} aria-hidden="true" />
              <span>{status.label}</span>
            </div>
            {state === "closed" && closed_why && <p class="notice fail" role="alert">{closed_why}</p>}
            <div class="readouts">
              <div class="readout">
                <span>Streaming</span>
                <div class="readout-row">
                  <span class={`lamp ${info?.streaming ? "on" : ""}`} aria-hidden="true" />
                  <span class="readout-unit" style={{ color: "var(--color-text)" }}>{info === null ? "unknown" : info.streaming ? "yes" : "no"}</span>
                </div>
              </div>
              <LedReadout label="FFT size" text={info ? String(info.fft_size) : null} unit="bins" width={5} />
              <LedReadout label="Rows per second" text={info ? String(info.rows_per_s) : null} unit="rows/s" width={2} />
              <LedReadout label="Sample rate" text={rate_sps !== undefined ? (rate_sps / 1e6).toFixed(3) : null} unit="MS/s" width={5} />
              <LedReadout label="Centre frequency" text={center_hz !== undefined ? (center_hz / 1e6).toFixed(6) : null} unit="MHz" width={10} />
              <LedReadout label="Rows dropped" text={String(dropped)} width={6} />
            </div>
          </Panel>
        </div>
      </main>
    </div>
  );
}

function Splash({ message }: { message: string }) {
  return (
    <div class="splash" role="alert">
      {message}
    </div>
  );
}

const style = document.createElement("style");
style.textContent = THEMES.map(themeCss).join("");
document.head.appendChild(style);
document.documentElement.dataset.theme = (THEMES.find((t) => t.id === query.get("theme")) ?? THEMES[0]).id;

const root = document.getElementById("app")!;
if (token_state.kind === "ok" || fake) {
  render(<App token={token_state.kind === "ok" ? token_state.token : null} />, root);
} else {
  render(<Splash message={token_state.message} />, root);
}
