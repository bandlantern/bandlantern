# ADR-008: WebSocket auth by first message; fixed binary spectrum frame

- **Status:** Accepted
- **Date:** 2026-11-08
- **Related:** ADR-007, ADR-010; contract in docs/api/; was ADR-014

## Context
Origin/Host checks stop web pages and DNS rebinding, but any local process can forge `Origin`,
so each socket needs the per-session token. Browsers cannot set custom headers on a WebSocket.
The spectrum stream needs a binary format that Python, C++ and TypeScript agree on.

## Decision
- **Auth:** the client's first WebSocket message is `hello{token}`; a wrong token, another first
  message, or none within 2000 ms → close 1008. Rejected: the token in `Sec-WebSocket-Protocol`
  (misuses the header, may be logged) and `?token=` in the URL (lands in logs and history).
- **Frames:** a 48-byte little-endian header (magic `BLF\0`, version, kind, header_len,
  payload_format, bin_count, seq, sample_index, center_freq_hz, rate_sps), then f32 dBFS bins,
  FFT-shifted. The header can grow by appending fields; readers seek to `header_len`.
- A committed golden frame (`testdata/api/spectrum_frame_v1.bin`, from
  `tools/py/make_frame_vector.py`) is the byte-level contract.
- Changed from the archive: new magic, no `view_id` field (zoom is A5 and adds it then), and the
  frame no longer fixes the window or the dBFS scaling: those belong to the owner's A0 block
  (ADR-002) and are written into frames.md when it lands.

## Consequences
- The engine holds unauthenticated sockets for up to 2 s and sends them nothing.
- f32 bins cost 8 KB per 2048-bin row (about 245 KB/s at 30 rows/s): fine on loopback.
- A format change updates the docs, the generator, and the golden vector in one commit.
