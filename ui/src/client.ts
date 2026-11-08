// WebSocket client. Contract: docs/api/README.md "Authenticating a socket", messages.md, frames.md.
// The socket is a dependency so tests and the in-browser demo can pass a fake.

import type { EngineCommand, HelloCommand, InfoEvent } from "./api.generated";
import { parseFrame, type SpectrumRow } from "./frames";

/** The subset of WebSocket used here. Cast a real WebSocket to this in main.ts. */
export interface WebSocketLike {
  binaryType: string;
  onopen: ((ev: unknown) => void) | null;
  onmessage: ((ev: { data: unknown }) => void) | null;
  onclose: ((ev: { code: number; reason: string }) => void) | null;
  send(data: string): void;
  close(code?: number, reason?: string): void;
}

export type ClientState = "connecting" | "authenticating" | "ready" | "closed";

/** Commands a caller may send; the client sends `hello` itself. */
export type CommandType = Exclude<EngineCommand["type"], "hello">;

// Omit applied to each member of a union separately (plain Omit merges the members).
type OmitEach<T, K extends PropertyKey> = T extends unknown ? Omit<T, K> : never;

/** Fields of command T without the envelope, which sendCommand adds. */
export type CommandFields<T extends CommandType> = OmitEach<
  Extract<EngineCommand, { type: T }>,
  "v" | "type" | "id"
>;

export interface ClientHandlers {
  onState?(state: ClientState): void;
  onRow?(row: SpectrumRow): void;
  onInfo?(info: InfoEvent): void;
  /** Rows the engine reports dropping (messages.md `drop`), one event's worth. */
  onDrop?(rows_dropped: number): void;
  /** re: the id of the command that failed, when the engine says. */
  onError?(code: string, message: string, re?: number): void;
  onLog?(message: string): void;
}

export class EngineClient {
  state: ClientState = "connecting";
  /** Rows missing from the seq sequence (gaps seen by this client). */
  seqGapRows = 0;
  /** Sum of rows_dropped from the engine's `drop` events. */
  engineReportedDropped = 0;
  /** Why the socket closed (README "Close codes": 1008 "auth" = wrong or missing token). */
  closeCode: number | null = null;
  closeReason = "";

  private nextSeq = 0;
  private nextId = 2; // id 1 is the hello
  private loggedReasons = new Set<string>();

  constructor(
    private socket: WebSocketLike,
    private token: string,
    private handlers: ClientHandlers = {},
  ) {
    socket.binaryType = "arraybuffer";
    socket.onopen = () => this.handleOpen();
    socket.onmessage = (ev) => this.handleMessage(ev.data);
    socket.onclose = (ev) => {
      this.closeCode = ev.code;
      this.closeReason = ev.reason;
      this.setState("closed");
    };
  }

  /**
   * Send a command (not `hello`); returns its id, or null if not ready. The fields
   * argument may be left out only when the command has no required fields.
   */
  sendCommand<T extends CommandType>(
    type: T,
    ...[fields]: {} extends CommandFields<T> ? [CommandFields<T>?] : [CommandFields<T>]
  ): number | null {
    return this.send(type, fields ?? {});
  }

  private send(type: string, fields: object): number | null {
    if (this.state !== "ready") {
      return null;
    }
    const id = this.nextId++;
    this.socket.send(JSON.stringify({ v: 1, type, id, ...fields }));
    return id;
  }

  close(): void {
    this.socket.close(1000);
  }

  private setState(state: ClientState): void {
    this.state = state;
    this.handlers.onState?.(state);
  }

  private handleOpen(): void {
    // Must be the first message on the socket (README, "Authenticating a socket" step 1).
    const hello: HelloCommand = { v: 1, type: "hello", id: 1, token: this.token };
    this.socket.send(JSON.stringify(hello));
    this.setState("authenticating");
  }

  private handleMessage(data: unknown): void {
    if (typeof data === "string") {
      this.handleText(data);
    } else if (data instanceof ArrayBuffer) {
      this.handleBinary(data);
    }
  }

  private handleText(text: string): void {
    let msg: { type?: unknown; [key: string]: unknown };
    try {
      msg = JSON.parse(text);
    } catch {
      this.logOnce("non-JSON text frame");
      return;
    }
    if (this.state === "authenticating") {
      if (msg.type === "hello_ok") {
        this.setState("ready");
      }
      return; // nothing else is processed before hello_ok
    }
    if (this.state !== "ready") {
      return;
    }
    switch (msg.type) {
      case "info":
        this.handlers.onInfo?.(msg as unknown as InfoEvent);
        break;
      case "drop":
        this.engineReportedDropped += Number(msg.rows_dropped) || 0;
        this.handlers.onDrop?.(Number(msg.rows_dropped) || 0);
        break;
      case "error":
        this.handlers.onError?.(String(msg.code), String(msg.message), typeof msg.re === "number" ? msg.re : undefined);
        break;
    }
  }

  private handleBinary(buffer: ArrayBuffer): void {
    if (this.state !== "ready") {
      return;
    }
    const result = parseFrame(buffer);
    if (result.status !== "row") {
      this.logOnce(`${result.status} frame: ${result.reason}`);
      return;
    }
    const row = result.row;
    // frames.md: seq is +1 per row produced, so a jump forward is a number of dropped rows.
    // A seq below the expected one (late/duplicate) is not counted and does not move the counter.
    if (row.seq >= this.nextSeq) {
      this.seqGapRows += row.seq - this.nextSeq;
      this.nextSeq = row.seq + 1;
    }
    this.handlers.onRow?.(row);
  }

  private logOnce(message: string): void {
    if (this.loggedReasons.has(message)) {
      return;
    }
    this.loggedReasons.add(message);
    this.handlers.onLog?.(message);
  }
}
