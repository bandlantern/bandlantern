"""WAV IQ writer/reader: round trips, chunk walking, committed vectors are current, excerpts."""

import json
import struct
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import wav_iq  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
STEP = {"u8": 1 / 127.5, "s16": 1 / 32768.0, "f32": 1e-7}


@pytest.mark.parametrize("sample_format", ["u8", "s16", "f32"])
@pytest.mark.parametrize("extensible", [False, True])
@pytest.mark.parametrize("extra_chunks", [False, True])
def test_round_trip_within_one_step(tmp_path, sample_format, extensible, extra_chunks):
    x = wav_iq.vector_samples()
    path = tmp_path / "x.wav"
    path.write_bytes(wav_iq.wav_bytes(x, sample_format, 48_000, extensible, extra_chunks))

    rate_sps, datatype, y = wav_iq.read_iq_wav(path)

    assert rate_sps == 48_000
    assert datatype == wav_iq.FORMATS[sample_format][2]
    assert np.max(np.abs(y - x)) <= STEP[sample_format]


def test_extra_chunks_are_padded_to_even_offsets():
    blob = wav_iq.wav_bytes(wav_iq.vector_samples(), "s16", 48_000, extra_chunks=True)
    # RIFF header (12) + LIST header (8) + 5 body bytes + 1 pad -> data chunk at 26.
    assert blob[12:16] == b"LIST"
    assert blob[26:30] == b"data"


def test_mono_is_rejected(tmp_path):
    fmt = struct.pack("<HHIIHH", wav_iq.FORMAT_PCM, 1, 48_000, 96_000, 2, 16)
    body = wav_iq.chunk(b"fmt ", fmt) + wav_iq.chunk(b"data", b"\x00\x00")
    path = tmp_path / "mono.wav"
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)
    with pytest.raises(ValueError, match="2-channel"):
        wav_iq.read_iq_wav(path)


def test_committed_vectors_are_current(tmp_path):
    committed = REPO / "testdata" / "wav"
    wav_iq.write_vectors(tmp_path)
    for name in list(wav_iq.VECTORS) + ["iq_vectors.json"]:
        assert (tmp_path / name).read_bytes() == (committed / name).read_bytes(), name


def test_excerpt_keeps_format_and_slices_data(tmp_path):
    x = np.exp(1j * 2 * np.pi * 0.01 * np.arange(1000))
    src = tmp_path / "baseband_100000000Hz_x.wav"
    src.write_bytes(wav_iq.wav_bytes(x, "s16", 1000, extra_chunks=True))
    dst = tmp_path / "clip" / "baseband_100000000Hz_x.wav"

    wav_iq.excerpt(src, dst, start_s=0.2, duration_s=0.1)

    rate_sps, datatype, y = wav_iq.read_iq_wav(dst)
    _, _, full = wav_iq.read_iq_wav(src)
    assert (rate_sps, datatype) == (1000, "cs16_le")
    assert np.array_equal(y, full[200:300])
    expected = json.loads(dst.with_suffix(".json").read_text())
    assert expected["samples"] == 100
    assert expected["first_samples"][0] == [y[0].real, y[0].imag]
