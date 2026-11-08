# Test data layout (SigMF, WAV)

Recordings and synthetic IQ use [SigMF](https://github.com/sigmf/SigMF) core v1.2:
a `<name>.sigmf-meta` JSON file plus a `<name>.sigmf-data` raw sample file with the same stem.
Readers and writers are our own, written from the spec (no SigMF library).

## Directory layout

```
testdata/
  api/                         golden API vectors (committed, see frames.md, messages.md)
  synth/                       output of the synthetic IQ generator
    <scenario>_<rate>.sigmf-meta   committed (small JSON)
    <scenario>_<rate>.sigmf-data   NOT committed (.gitignore); regenerate
  wav/                         small WAV IQ golden vectors + iq_vectors.json (committed)
  captures/                    real recordings, data files NOT committed
```

- `<rate>` is a short tag, e.g. `2m4` = 2.4 MS/s.
- `.sigmf-data` files are git-ignored. Synthetic data is regenerated deterministically:
  `.venv/bin/python tools/py/synth_iq.py --out testdata/synth` (same seed gives identical bytes).
- Tests that need IQ generate it into a temp dir, or depend on the generator having run.

## Supported datatypes

| `core:datatype` | Layout | To cf32 |
|---|---|---|
| `cu8` | interleaved u8 I, Q | `(b - 127.5) / 127.5` |
| `cf32_le` | interleaved f32 I, Q | as is |

The generator writes **`cu8`** (what the RTL dongle and `rtl_tcp` produce), so the playback
path is the same for synthetic and real captures. Anything else, including `cs16_le` in a
SigMF file (WAV only), gives `bad_format`.

## Required metadata

```json
{
  "global": {
    "core:datatype": "cu8",
    "core:sample_rate": 2400000,
    "core:version": "1.2.0",
    "core:description": "Three tones + noise, seed 1",
    "core:recorder": "bandlantern synth_iq.py"
  },
  "captures": [
    { "core:sample_start": 0, "core:frequency": 100000000 }
  ],
  "annotations": [
    {
      "core:sample_start": 0,
      "core:freq_lower_edge": 100149500,
      "core:freq_upper_edge": 100150500,
      "core:label": "tone -20 dBFS"
    }
  ]
}
```

- Engine **requires** `global.core:datatype`, `global.core:sample_rate` (finite, > 0) and
  `captures[0].core:frequency` (Hz, centre frequency). Missing or wrong type gives `bad_format`.
- The reader uses only the first capture's frequency; later captures (retunes) are ignored
  (the `IIqSource` interface has no per-read frequency).
- `annotations` mark what the generator put where (frequency edges in Hz). The engine ignores
  them; tests use them as ground truth.
- A data file whose size is not a whole number of samples drops the torn tail. A zero-length
  data file opens and reads 0 samples. A missing `.sigmf-meta` or `.sigmf-data` is `not_found`;
  a meta file over 16 MiB, or a path that is not a regular file, is `bad_format`.

## WAV IQ

Many SDR programs record baseband IQ as a WAV file: a RIFF/WAVE file whose two channels are
I (left/first) and Q (right/second). Our reader and writer (`engine/io/wav.*`,
`tools/py/wav_iq.py`) follow the Microsoft/IBM *Multimedia Programming Interface and Data
Specifications 1.0* (1991) and Microsoft's `WAVE_FORMAT_EXTENSIBLE` notes.

| fmt (tag, bits) | `datatype` | To cf32 |
|---|---|---|
| PCM (1), 8 | `cu8` | `(b - 127.5) / 127.5`, same as SigMF `cu8` |
| PCM (1), 16 | `cs16_le` | `s / 32768` |
| IEEE float (3), 32 | `cf32_le` | as is |

- `WAVE_FORMAT_EXTENSIBLE` (0xFFFE) is accepted when its SubFormat GUID is one of the two tags above.
- Exactly 2 channels; `nBlockAlign` must be `2 * bits / 8`. Anything else gives `bad_format`.
  Mono (demodulated audio, not IQ) is rejected.
- Chunks may come in any order; unknown chunks are skipped (odd sizes padded to even). A
  `data` size beyond the end of the file is clamped to the file; the RIFF size field is ignored.
  RF64 (> 4 GB) gives `bad_format`.
- 8-bit WAV uses the `cu8` midpoint 127.5, not the WAV spec's 128: 8-bit IQ WAVs are RTL `cu8`
  bytes written verbatim, and the half-LSB difference is only a DC offset.
- **Centre frequency:** WAV has no standard field for it. The engine takes the first
  `<digits>Hz` token between `_` separators in the file name, e.g.
  `baseband_162475000Hz_13-04-51_26-10-2026.wav` gives 162 475 000 Hz; otherwise `0`.
  Vendor metadata chunks have no public spec and are skipped as unknown chunks.

Regenerate the vectors with `.venv/bin/python tools/py/wav_iq.py vectors --out testdata/wav`; cut a clip
with `.venv/bin/python tools/py/wav_iq.py excerpt SRC.wav DST.wav --start-s 10 --duration-s 0.1`
(keep the original file name so the centre frequency still parses).

## Synthetic scenarios

| Stem | Content | Duration |
|---|---|---|
| `tones_2m4` | 2.4 MS/s, centre 100 MHz; complex tones at +150 kHz (-20 dBFS), -400 kHz (-40 dBFS), +900 kHz (-60 dBFS); complex white noise at -70 dBFS total; seed 1 | 2 s |
| `sweep_2m4` | 2.4 MS/s, centre 100 MHz; one -20 dBFS tone sweeping linearly -1.0 to +1.0 MHz over the file; noise as above; seed 2 | 4 s |

Levels are before `cu8` quantization; 8-bit quantization noise (about -50 dBFS spread over the
band) is expected and fine. Tone start phases are drawn from the scenario's seeded RNG.
