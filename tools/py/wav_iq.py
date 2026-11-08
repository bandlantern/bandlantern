"""WAV IQ files (docs/api/testdata.md, "WAV IQ"): writer, reference reader, golden vectors, excerpts.

Written from the Microsoft/IBM "Multimedia Programming Interface and Data Specifications 1.0"
(1991; RIFF chunks, WAVE fmt/data) and Microsoft's WAVE_FORMAT_EXTENSIBLE documentation.
Standard library + NumPy only (the stdlib `wave` module cannot write float or EXTENSIBLE).

Usage:
  .venv/bin/python tools/py/wav_iq.py vectors --out testdata/wav
  .venv/bin/python tools/py/wav_iq.py excerpt SRC.wav DST.wav --start-s 10 --duration-s 0.1
"""

import argparse
import json
import struct
from pathlib import Path

import numpy as np

FORMAT_PCM = 0x0001
FORMAT_IEEE_FLOAT = 0x0003
FORMAT_EXTENSIBLE = 0xFFFE
# KSDATAFORMAT_SUBTYPE_* GUID tail: {xxxx0000-0000-0010-8000-00AA00389B71}, stored little-endian.
GUID_TAIL = bytes([0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71])

# sample_format -> (format tag, bits per sample, testdata.md datatype name)
FORMATS = {
    "u8": (FORMAT_PCM, 8, "cu8"),
    "s16": (FORMAT_PCM, 16, "cs16_le"),
    "f32": (FORMAT_IEEE_FLOAT, 32, "cf32_le"),
}

VECTOR_RATE_SPS = 48_000
VECTOR_SAMPLES = 16


def chunk(chunk_id, body):
    """RIFF chunk: id, u32 LE size, body, pad byte if the size is odd."""
    pad = b"\x00" if len(body) % 2 else b""
    return chunk_id + struct.pack("<I", len(body)) + body + pad


def fmt_body(sample_format, rate_sps, extensible):
    tag, bits, _ = FORMATS[sample_format]
    channels = 2
    block_align = channels * bits // 8
    tag_field = FORMAT_EXTENSIBLE if extensible else tag
    base = struct.pack("<HHIIHH", tag_field, channels, rate_sps, rate_sps * block_align, block_align, bits)
    if not extensible:
        # Non-PCM fmt chunks carry cbSize (0 here); PCM ones may omit it.
        return base if tag == FORMAT_PCM else base + struct.pack("<H", 0)
    channel_mask = 0x3  # front left (I), front right (Q)
    sub_format = struct.pack("<H", tag) + GUID_TAIL
    return base + struct.pack("<HHI", 22, bits, channel_mask) + sub_format


def encode_samples(x, sample_format):
    """cf32 -> interleaved I,Q bytes; inverse of the testdata.md WAV formulas."""
    iq = np.empty(2 * len(x), dtype=np.float64)
    iq[0::2] = x.real
    iq[1::2] = x.imag
    if sample_format == "u8":
        return np.clip(np.round(iq * 127.5 + 127.5), 0, 255).astype(np.uint8).tobytes()
    if sample_format == "s16":
        return np.clip(np.round(iq * 32768.0), -32768, 32767).astype("<i2").tobytes()
    return iq.astype("<f4").tobytes()


def wav_bytes(x, sample_format, rate_sps, extensible=False, extra_chunks=False):
    """Whole file. extra_chunks: odd-sized LIST before fmt, data before fmt, trailing chunk."""
    fmt = chunk(b"fmt ", fmt_body(sample_format, rate_sps, extensible))
    data = chunk(b"data", encode_samples(x, sample_format))
    if extra_chunks:
        body = chunk(b"LIST", b"INFOx") + data + fmt + chunk(b"junk", b"\x01\x02\x03")
    else:
        body = fmt + data
    return b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body


def read_iq_wav(path):
    """Reference reader: returns (rate_sps, datatype, cf32 samples as complex128)."""
    blob = Path(path).read_bytes()
    if blob[0:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    fmt = None
    data = None
    pos = 12
    while pos + 8 <= len(blob):
        chunk_id = blob[pos : pos + 4]
        (size,) = struct.unpack_from("<I", blob, pos + 4)
        body = blob[pos + 8 : pos + 8 + size]
        if chunk_id == b"fmt ":
            fmt = body
        elif chunk_id == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("missing fmt or data chunk")
    tag, channels, rate_sps, _, block_align, bits = struct.unpack_from("<HHIIHH", fmt)
    if tag == FORMAT_EXTENSIBLE:
        (tag,) = struct.unpack_from("<H", fmt, 24)
    if channels != 2:
        raise ValueError("not 2-channel IQ")
    data = data[: len(data) // block_align * block_align]
    if (tag, bits) == (FORMAT_PCM, 8):
        iq = (np.frombuffer(data, dtype=np.uint8).astype(np.float64) - 127.5) / 127.5
        datatype = "cu8"
    elif (tag, bits) == (FORMAT_PCM, 16):
        iq = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
        datatype = "cs16_le"
    elif (tag, bits) == (FORMAT_IEEE_FLOAT, 32):
        iq = np.frombuffer(data, dtype="<f4").astype(np.float64)
        datatype = "cf32_le"
    else:
        raise ValueError(f"unsupported format tag {tag} with {bits} bits")
    return rate_sps, datatype, iq[0::2] + 1j * iq[1::2]


def vector_samples():
    """Fixed pattern: both full-scale ends, zero, and a few in-between values."""
    i = np.array([-1.0, 1.0, 0.0, 0.5, -0.5, 0.25, -0.25, 0.999, -0.999, 0.1, -0.1, 0.75, -0.75, 0.3, -0.3, 0.0])
    q = np.roll(i, 5)
    return i + 1j * q


# file name -> (sample_format, extensible, extra_chunks)
VECTORS = {
    "iq_u8.wav": ("u8", False, False),
    "iq_s16.wav": ("s16", False, False),
    "iq_f32.wav": ("f32", False, False),
    "iq_s16_extensible.wav": ("s16", True, False),
    "iq_f32_extensible.wav": ("f32", True, False),
    "iq_s16_chunks.wav": ("s16", False, True),
}


def write_vectors(out_dir):
    """Small WAV files + iq_vectors.json with the cf32 values a reader must produce."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    expected = {}
    for name, (sample_format, extensible, extra_chunks) in VECTORS.items():
        path = out_dir / name
        path.write_bytes(wav_bytes(vector_samples(), sample_format, VECTOR_RATE_SPS, extensible, extra_chunks))
        rate_sps, datatype, x = read_iq_wav(path)
        expected[name] = {
            "rate_sps": rate_sps,
            "datatype": datatype,
            "samples": [[float(s.real), float(s.imag)] for s in x],
        }
    (out_dir / "iq_vectors.json").write_text(json.dumps(expected, indent=1) + "\n")


def summary(path):
    """What a reader must report for a clip: header fields, sample count, a few samples, power."""
    rate_sps, datatype, x = read_iq_wav(path)
    return {
        "rate_sps": rate_sps,
        "datatype": datatype,
        "samples": len(x),
        "first_samples": [[float(s.real), float(s.imag)] for s in x[:8]],
        "mean_power": float(np.mean(np.abs(x) ** 2)),
    }


def excerpt(src, dst, start_s, duration_s):
    """Copy a slice of a WAV IQ file's data chunk under a fresh 44-byte-style header.

    Keeps the original fmt chunk verbatim, so format, rate and channel layout are unchanged.
    Writes <dst>.json (summary()) next to it as the golden expectation.
    """
    blob = Path(src).read_bytes()
    fmt = None
    data_pos = data_size = None
    pos = 12
    while pos + 8 <= len(blob) and (fmt is None or data_pos is None):
        chunk_id = blob[pos : pos + 4]
        (size,) = struct.unpack_from("<I", blob, pos + 4)
        if chunk_id == b"fmt ":
            fmt = blob[pos + 8 : pos + 8 + size]
        elif chunk_id == b"data":
            data_pos, data_size = pos + 8, min(size, len(blob) - pos - 8)
        pos += 8 + size + (size & 1)
    if fmt is None or data_pos is None:
        raise ValueError("missing fmt or data chunk")
    _, _, rate_sps, _, block_align, _ = struct.unpack_from("<HHIIHH", fmt)
    first = int(round(start_s * rate_sps)) * block_align
    count = int(round(duration_s * rate_sps)) * block_align
    if first + count > data_size:
        raise ValueError("excerpt runs past the end of the data chunk")
    body = chunk(b"fmt ", fmt) + chunk(b"data", blob[data_pos + first : data_pos + first + count])
    dst = Path(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)
    dst.with_suffix(".json").write_text(json.dumps(summary(dst), indent=1) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    vectors = sub.add_parser("vectors", help="write small golden WAV files + iq_vectors.json")
    vectors.add_argument("--out", required=True)
    cut = sub.add_parser("excerpt", help="cut a short clip from a WAV IQ capture")
    cut.add_argument("src")
    cut.add_argument("dst")
    cut.add_argument("--start-s", type=float, default=0.0)
    cut.add_argument("--duration-s", type=float, default=0.1)
    args = parser.parse_args()
    if args.command == "vectors":
        write_vectors(args.out)
        print(f"wrote {len(VECTORS)} files + iq_vectors.json to {args.out}")
    else:
        excerpt(args.src, args.dst, args.start_s, args.duration_s)
        print(f"wrote {args.dst} and {Path(args.dst).with_suffix('.json')}")


if __name__ == "__main__":
    main()
