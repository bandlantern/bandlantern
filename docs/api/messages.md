# JSON messages — v0.1

All control traffic is WebSocket **text** frames, one JSON object per frame.
Shapes are defined by [schema/api.schema.json](schema/api.schema.json) (README "Schema");
this file has the rules and meaning. Examples: `testdata/api/messages/`.

## Envelope

```json
{ "v": 1, "type": "set_fft", "id": 7, "size": 2048 }
```

| Field | Type | Rules |
|---|---|---|
| `v` | int | Envelope version, always `1`. Other values → `error` `bad_version`. |
| `type` | string | Message type, `snake_case`. |
| `id` | int | **Commands only**, required. Client-chosen, `1 ≤ id ≤ 2^31-1`. |
| `re` | int | **Replies only**: the `id` of the command being answered. |

- Every command gets **exactly one** reply: its success event (with `re`) or an `error`
  (with `re`). Replies to one socket arrive in command order.
- Unsolicited events have no `re`.
- `v`, `type`, `id`, and `re` are reserved: no message type may use them as payload fields.
- Unknown fields are ignored. Unknown `type` → `error` `unknown_type`.
- Units are in field names (`_hz`, `_sps`, `_ms`, `_s`). Frequencies and rates are JSON
  numbers (may be non-integer).

## Commands (client → engine)

### `hello`
First message on every socket (README "Authenticating a socket").
```json
{ "v": 1, "type": "hello", "id": 1, "token": "q3J1...43 chars" }
```
Reply: `hello_ok`. Failure: socket closed with 1008 (no `error` event, no hint).

### `get_info`
```json
{ "v": 1, "type": "get_info", "id": 2 }
```
Reply: `info`.

### `play`
Start a SigMF or WAV IQ file as the source. Replaces any current source.
```json
{ "v": 1, "type": "play", "id": 3, "path": "synth/tones_2m4.sigmf-meta", "loop": true }
```
| Field | Type | Rules |
|---|---|---|
| `path` | string | Relative to the engine's data root (`--data-root`, default `./testdata`). Must name a `.sigmf-meta` or `.wav` file. Absolute paths, `..` segments, and symlinks leaving the root → `bad_path`. |
| `loop` | bool | Optional, default `false`. Restart at end of file. |

Playback is paced to real time (the file's sample rate).
Reply: `info` (with `source` filled and `streaming: true`).
Errors: `bad_path`, `not_found` (meta or its `.sigmf-data` missing), `bad_format` (meta invalid
or datatype not supported).

### `stop`
```json
{ "v": 1, "type": "stop", "id": 4 }
```
Stops the source; spectrum frames stop. Reply: `info` (`source: null`, `streaming: false`).
Not an error if nothing is playing.

### `set_fft`
```json
{ "v": 1, "type": "set_fft", "id": 5, "size": 2048, "rows_per_s": 30 }
```
| Field | Type | Rules |
|---|---|---|
| `size` | int | Optional. Power of two, 256 … 16384. Default 2048. |
| `rows_per_s` | number | Optional. 1 … 60. Default 30. |

Missing fields keep their current value. While a file plays, the pipeline restarts with the new
settings **from the start of the file** (`sample_index` restarts at 0). Reply: `info`.
Error: `bad_value`.

## Events (engine → client)

### `hello_ok`
```json
{ "v": 1, "type": "hello_ok", "re": 1, "api_version": "0.1", "engine_version": "0.0.0" }
```

### `info`
Reply to `get_info`, `play`, `stop`, `set_fft`. Also sent unsolicited (no `re`) to every socket
when the source ends without `loop`, and after a `source_error`.
```json
{
  "v": 1, "type": "info", "re": 3,
  "streaming": true,
  "fft_size": 2048,
  "rows_per_s": 30,
  "source": {
    "kind": "sigmf",
    "path": "synth/tones_2m4.sigmf-meta",
    "datatype": "cu8",
    "rate_sps": 2400000,
    "center_freq_hz": 100000000,
    "loop": true
  }
}
```
`source` is `null` when nothing is playing. `kind` is `"sigmf"` or `"wav"`; `datatype` is
`"cu8"`, `"cs16_le"` (WAV only) or `"cf32_le"`. For WAV, `center_freq_hz` comes from the file
name, or is `0` if the name has none.

### `drop`
The engine discarded spectrum rows because this client was not keeping up (frames.md
"Back-pressure"). Sent at most once per second, only if rows were dropped since the last one.
```json
{ "v": 1, "type": "drop", "rows_dropped": 12 }
```

### `error`
```json
{ "v": 1, "type": "error", "re": 3, "code": "not_found", "message": "synth/x.sigmf-meta does not exist" }
```
`re` is absent for errors not tied to a command. `message` is for humans; clients branch on `code`.

| `code` | Meaning |
|---|---|
| `bad_version` | Envelope `v` not supported |
| `unknown_type` | Unknown `type` |
| `bad_request` | Missing/mistyped required field, or missing/invalid `id` on a command |
| `bad_value` | Field present but out of range |
| `bad_path` | `play.path` not allowed |
| `not_found` | `play.path` does not exist |
| `bad_format` | File unreadable as SigMF/WAV IQ, or unsupported datatype |
| `source_error` | Read error during playback (the source stops; `info` follows) |
| `internal` | Engine bug; details in the engine log, never in `message` |
