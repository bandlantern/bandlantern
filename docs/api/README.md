# Bandlantern Engine API — v0.1

The contract between the engine (C++) and every client (the browser UI, later the Electron
shell). Decisions: ADR-007 (local server), ADR-008 (socket auth, frame format), ADR-009
(untrusted clients), ADR-010 (JSON plus binary).

| File | Covers |
|---|---|
| [README.md](README.md) | Transport, security, token bootstrap, versioning, schema |
| [messages.md](messages.md) | JSON commands and events |
| [frames.md](frames.md) | Binary frame header and spectrum payload |
| [schema/api.schema.json](schema/api.schema.json) | JSON Schema of every JSON message |
| [../../engine/pipeline/pipeline.hpp](../../engine/pipeline/pipeline.hpp) | The seam the owner's A0 pipeline plugs into |

Golden vector: `testdata/api/spectrum_frame_v1.bin`, produced by
`tools/py/make_frame_vector.py`. Every implementation (Python, C++, TypeScript) must produce or
parse it byte for byte. If this document and the vector disagree, fix both in one commit.

## Versioning

- **API version** is `"0.1"` (string, `major.minor`), reported in `hello_ok`.
- **minor** bump: additive change (new message type, new optional field, new header field
  appended after the existing ones). Old clients keep working.
- **major** bump: anything else (renamed or removed field, changed meaning or layout).
- JSON: every message carries `"v": 1` (envelope version). Receivers **ignore unknown fields**.
- Binary: the header carries its own `version` (u16) and `header_len`; see frames.md.
- While the API is `0.x`, breaking changes are allowed but bump the minor and update the
  golden vector in the same commit.

## Schema

- `schema/api.schema.json` is the source of truth for the **shape** of every JSON message
  (fields, types, ranges, required/optional). messages.md holds the rules and meaning.
- `ui/src/api.generated.ts` is generated from it; never edit it by hand. After a schema change
  run `.venv/bin/python tools/py/api_schema.py` and commit the schema, the generated file,
  messages.md, and `testdata/api/messages/` together (bump `x-api-version` per "Versioning").
- `.venv/bin/pytest tools/py` (CI) fails if the generated file is stale, if the schema and
  messages.md list different messages or error codes, or if an example in
  `testdata/api/messages/` does not validate (`invalid.json`: does not fail for the stated reason).
- Only the keyword subset listed in `tools/py/api_schema.py` is allowed; anything else is an
  error, so the generator never silently drops a rule.

## Transport

- The engine binds **only** `127.0.0.1` (never `0.0.0.0`; remote access is off, ADR-009).
- Port: `--port <n>`, default **8780**.
- `GET /` and static paths serve the UI bundle (`--static-root <dir>`, default `ui/dist`).
  Paths with `..` segments or percent-encoded bytes → `403`.
- `GET /ws` upgrades to a WebSocket. One socket carries both JSON (text frames) and spectrum
  data (binary frames).
- At most **8** authenticated sockets; a further one is closed with **1011**, reason `"busy"`.
- Text frames: UTF-8 JSON, one message per frame, max **65536 bytes**. Binary frames from the
  client are a protocol error.
- Liveness: if the engine receives nothing from a socket (data or pong) for **15 s**, it sends
  a ping; with still nothing by **30 s**, it drops the TCP connection without a close frame.
  Browsers answer pings automatically.
- No CORS headers are ever sent (no `Access-Control-Allow-Origin`, especially no `*`).

## Request checks (HTTP and WebSocket upgrade)

Rejected requests get `403` (HTTP) or the upgrade is refused with `403`; nothing else is
revealed. With `<port>` the engine's actual port:

| Header | Allowed values (exact, case-insensitive host) |
|---|---|
| `Host` | `127.0.0.1:<port>`, `localhost:<port>` |
| `Origin` (required on `/ws`) | `http://127.0.0.1:<port>`, `http://localhost:<port>`, plus each `--dev-origin <origin>` |

- The `Host` check blocks DNS rebinding (`evil.example` resolving to 127.0.0.1 still sends
  `Host: evil.example`).
- The `Origin` check blocks other web pages; browsers do not let pages forge it.
- `--dev-origin http://localhost:5173` is for the Vite dev server. Off by default; repeatable.
- Plain HTTP requests without `Origin` are allowed (browsers omit it on same-origin `GET`);
  when present it must be in the table.
- A WebSocket upgrade **without** `Origin` is refused. Non-browser clients must still send one
  of the allowed origins; the token is what stops them (below).

## Token

- At startup the engine generates **32 random bytes** from the OS CSPRNG, encoded as base64url
  without padding (43 chars). New token every engine start.
- The token is **never** served over HTTP, written to files, or logged.
- Comparison is constant-time.

### Bootstrap in a plain browser

1. The engine starts listening, then opens the system browser at
   `http://127.0.0.1:<port>/#token=<token>`. The fragment is not sent to the server.
2. If it cannot open a browser (e.g. under WSL), it does nothing unless started with
   `--print-url`, which prints the URL to **its own stderr** only.
3. The page reads `location.hash`, stores the token in `sessionStorage` under
   `bandlantern.token`, and clears the fragment with `history.replaceState` before anything
   else runs.
4. On reload the page uses `sessionStorage`. No token → show "Open the link from the engine".

A desktop shell (later) injects the token instead; the socket protocol below is unchanged.

### Authenticating a socket (ADR-008)

1. After the upgrade, the client's **first** message must be
   `{"v":1,"type":"hello","id":1,"token":"<token>"}`.
2. The engine replies `hello_ok` (messages.md). Before that it sends nothing.
   Any other first message, a wrong token, or no `hello` within **2000 ms** after the upgrade
   → close with code **1008** (policy violation), reason `"auth"`. A first message that is not
   valid JSON is a protocol error like any other (1002, see "Close codes").
3. Only authenticated sockets receive events or binary frames.

## Trust model (ADR-009)

- Every client is untrusted, including our own UI. Anyone can read this protocol in browser
  DevTools or the UI bundle, and can write a client that sends any message.
- The engine checks each command itself: validation and safety interlocks (bias-tee, later). A
  command the session may not run gets an error reply, never a silent success. The UI hiding a
  control is a convenience, not a check.
- Plain `ws://` is acceptable only because the engine binds loopback.

## Close codes

| Code | When |
|---|---|
| 1000 | Normal close (either side) |
| 1002 | Protocol error: client sent a binary frame, non-JSON text, or a text frame > 65536 bytes |
| 1008 | Auth failed or timed out |
| 1011 | Engine internal error, or socket limit reached (reason `"busy"`) |
