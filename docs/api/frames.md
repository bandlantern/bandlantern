# Binary frames — v1

Bulk data is sent as WebSocket **binary** frames, engine → client only, one frame per
WebSocket message. v1 defines one kind: a spectrum row.

## Header (48 bytes, little-endian, all fields naturally aligned)

| Offset | Size | Type | Field | Value / meaning |
|---:|---:|---|---|---|
| 0 | 4 | bytes | `magic` | `42 4C 46 00` (`"BLF\0"`) |
| 4 | 2 | u16 | `version` | `1` |
| 6 | 2 | u16 | `kind` | `1` = spectrum row |
| 8 | 2 | u16 | `header_len` | `48`. The payload starts at this offset. |
| 10 | 2 | u16 | `payload_format` | `1` = f32 dBFS (below) |
| 12 | 4 | u32 | `bin_count` | Number of bins in the payload (= FFT size) |
| 16 | 8 | u64 | `seq` | Row counter per socket: starts at 0 after `hello_ok`, +1 per row **produced** (gaps = dropped rows). Never resets while the socket is open (not on `play` or `set_fft`). |
| 24 | 8 | u64 | `sample_index` | Index, in source samples since the pipeline started, of the first sample that went into this row. Restarts at 0 on `play` and `set_fft`; keeps counting across loops. |
| 32 | 8 | f64 | `center_freq_hz` | RF frequency of the centre bin (index `bin_count / 2`) |
| 40 | 8 | f64 | `rate_sps` | Complex sample rate the FFT ran at |
| 48 | … | | payload | `bin_count × 4` bytes |

Total frame length = `header_len + bin_count × 4`.

### Reader rules

- Reject (ignore the frame, log once) if `magic` differs, `version != 1`, `header_len < 48`,
  or the frame length is not `header_len + payload size`.
- Ignore frames with an unknown `kind` or `payload_format`.
- Always seek to `header_len` for the payload: minor API versions may append header fields
  (`header_len` grows, `version` stays 1).
- Rows from before a `play`/`set_fft` reply may still arrive after it. Use
  `bin_count`/`center_freq_hz`/`rate_sps` from each frame, not from the last `info`.
- JavaScript: read `seq` and `sample_index` with `getBigUint64`, or accept that `Number` is
  exact only up to 2^53 (about 119 years of samples at 2.4 MS/s).

## Spectrum row (`kind = 1`, `payload_format = 1`)

Payload: `bin_count` little-endian IEEE-754 **f32**, power in **dBFS**, **FFT-shifted**
(lowest frequency first, DC at index `bin_count / 2`). Every value is finite (no NaN, no ±inf).

Bin `i` frequency: `center_freq_hz + (i - bin_count/2) × rate_sps / bin_count`.

### How a row is computed

Owned by the A0 spectrum block (docs/learning/CURRICULUM.md, tutor mode): the window, the
scaling that defines 0 dBFS, averaging, overlap, and the floor. Filled in here when A0's C++
port lands, with the same numbers the A0 post derives. Until then a client may assume only the
rules above.

## Back-pressure

- The engine never blocks the DSP thread on a client.
- Each socket has a bounded queue of **8** rows. When it is full, the **new** row is dropped
  and counted; `seq` still increments, so the client sees the gap.
- The count is reported with the `drop` event (messages.md); each event carries the rows
  dropped since the previous one, so clients add them up.

## Golden vector

`testdata/api/spectrum_frame_v1.bin` (committed) = one spectrum frame with:

| Field | Value |
|---|---|
| `bin_count` | 8 |
| `seq` | 7 |
| `sample_index` | 160000 |
| `center_freq_hz` | 100100000.0 |
| `rate_sps` | 2400000.0 |
| payload | `[-200.0, -120.5, -90.25, -60.0, 0.0, -3.0103, -45.5, -110.75]` |

Built by `.venv/bin/python tools/py/make_frame_vector.py`; checked by
`tools/py/tests/test_frame_vector.py` and `engine/tests/api_frame_test.cpp`. C++ must serialize
these values to identical bytes; TypeScript must parse the file back to them. Payload values
are stored as f32, so compare against the f32-rounded value (`Math.fround(-3.0103)` in TS,
`-3.0103f` in C++), not the f64 literal.
