"""Build the golden spectrum frame (docs/api/frames.md, "Golden vector").

Python is the reference serializer; C++ and TypeScript tests must match the bytes
this writes to testdata/api/spectrum_frame_v1.bin.

Usage: .venv/bin/python tools/py/make_frame_vector.py [output_path]
"""

import struct
import sys
from pathlib import Path

MAGIC = b"BLF\x00"
VERSION = 1
KIND_SPECTRUM = 1
HEADER_LEN = 48
PAYLOAD_F32_DBFS = 1

# Layout from docs/api/frames.md: magic, version, kind, header_len, payload_format,
# bin_count, seq, sample_index, center_freq_hz, rate_sps. Little-endian, no padding.
HEADER_FORMAT = "<4sHHHHIQQdd"

GOLDEN_SEQ = 7
GOLDEN_SAMPLE_INDEX = 160_000
GOLDEN_CENTER_FREQ_HZ = 100_100_000.0
GOLDEN_RATE_SPS = 2_400_000.0
GOLDEN_POWER_DBFS = [-200.0, -120.5, -90.25, -60.0, 0.0, -3.0103, -45.5, -110.75]

DEFAULT_OUTPUT = Path(__file__).resolve().parents[2] / "testdata" / "api" / "spectrum_frame_v1.bin"


def spectrum_frame(seq, sample_index, center_freq_hz, rate_sps, power_dbfs):
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        KIND_SPECTRUM,
        HEADER_LEN,
        PAYLOAD_F32_DBFS,
        len(power_dbfs),
        seq,
        sample_index,
        center_freq_hz,
        rate_sps,
    )
    payload = struct.pack(f"<{len(power_dbfs)}f", *power_dbfs)
    return header + payload


def golden_frame():
    return spectrum_frame(
        GOLDEN_SEQ,
        GOLDEN_SAMPLE_INDEX,
        GOLDEN_CENTER_FREQ_HZ,
        GOLDEN_RATE_SPS,
        GOLDEN_POWER_DBFS,
    )


def main():
    output_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(golden_frame())
    print(f"wrote {output_path}")


if __name__ == "__main__":
    main()
