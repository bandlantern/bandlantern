"""Golden frame vector: committed bytes match the reference serializer, and every
offset in docs/api/frames.md holds the documented value."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import make_frame_vector as fv  # noqa: E402


def test_committed_vector_matches_serializer():
    assert fv.DEFAULT_OUTPUT.read_bytes() == fv.golden_frame()


def test_header_offsets_match_frames_md():
    frame = fv.DEFAULT_OUTPUT.read_bytes()

    assert struct.calcsize(fv.HEADER_FORMAT) == 48
    assert frame[0:4] == b"BLF\x00"
    assert struct.unpack_from("<H", frame, 4)[0] == 1
    assert struct.unpack_from("<H", frame, 6)[0] == 1
    assert struct.unpack_from("<H", frame, 8)[0] == 48
    assert struct.unpack_from("<H", frame, 10)[0] == 1
    assert struct.unpack_from("<I", frame, 12)[0] == 8
    assert struct.unpack_from("<Q", frame, 16)[0] == 7
    assert struct.unpack_from("<Q", frame, 24)[0] == 160_000
    assert struct.unpack_from("<d", frame, 32)[0] == 100_100_000.0
    assert struct.unpack_from("<d", frame, 40)[0] == 2_400_000.0


def test_payload_is_f32_dbfs_after_header():
    frame = fv.DEFAULT_OUTPUT.read_bytes()
    bin_count = 8

    assert len(frame) == 48 + bin_count * 4
    power_dbfs = struct.unpack_from(f"<{bin_count}f", frame, 48)
    expected = struct.unpack(f"<{bin_count}f", struct.pack(f"<{bin_count}f", *fv.GOLDEN_POWER_DBFS))
    assert power_dbfs == expected
