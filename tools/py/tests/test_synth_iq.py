"""Synthetic IQ generator: determinism, required SigMF metadata, tones where annotated."""

import json
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import synth_iq  # noqa: E402

N = 2048
BIN_WIDTH_HZ = synth_iq.RATE_SPS / N


@pytest.fixture(scope="module")
def tones_dir(tmp_path_factory):
    out = tmp_path_factory.mktemp("synth")
    synth_iq.write_scenario("tones_2m4", out)
    return out


def test_same_seed_gives_identical_bytes(tones_dir, tmp_path):
    synth_iq.write_scenario("tones_2m4", tmp_path)

    for suffix in (".sigmf-meta", ".sigmf-data"):
        assert (tones_dir / f"tones_2m4{suffix}").read_bytes() == (tmp_path / f"tones_2m4{suffix}").read_bytes()


def test_sweep_first_chunk_is_deterministic():
    first_a = next(synth_iq.scenario_cu8("sweep_2m4"))
    first_b = next(synth_iq.scenario_cu8("sweep_2m4"))

    assert first_a == first_b


def test_data_length_matches_duration(tones_dir):
    size = (tones_dir / "tones_2m4.sigmf-data").stat().st_size

    assert size == 2 * synth_iq.RATE_SPS * 2  # 2 s, 2 bytes per cu8 sample


def test_meta_required_keys_and_values(tones_dir):
    meta = json.loads((tones_dir / "tones_2m4.sigmf-meta").read_text())

    assert meta["global"]["core:datatype"] == "cu8"
    assert meta["global"]["core:sample_rate"] == 2_400_000
    assert meta["global"]["core:version"] == "1.2.0"
    assert meta["global"]["core:description"] == "Three tones + noise, seed 1"
    assert meta["global"]["core:recorder"] == "bandlantern synth_iq.py"
    assert meta["captures"] == [{"core:sample_start": 0, "core:frequency": 100_000_000}]
    assert len(meta["annotations"]) == 3
    first = meta["annotations"][0]
    assert first["core:freq_lower_edge"] == 100_149_500
    assert first["core:freq_upper_edge"] == 100_150_500
    assert first["core:label"] == "tone -20 dBFS"


def test_sweep_meta_has_one_annotation_covering_range():
    meta = synth_iq.SCENARIOS["sweep_2m4"]["meta"]

    assert meta["global"]["core:sample_rate"] == 2_400_000
    assert len(meta["annotations"]) == 1
    assert meta["annotations"][0]["core:freq_lower_edge"] == 99_000_000
    assert meta["annotations"][0]["core:freq_upper_edge"] == 101_000_000


def test_committed_meta_matches_generator():
    synth_dir = Path(__file__).resolve().parents[3] / "testdata" / "synth"
    for stem, spec in synth_iq.SCENARIOS.items():
        committed = json.loads((synth_dir / f"{stem}.sigmf-meta").read_text())
        assert committed == spec["meta"]


def test_tones_at_annotated_frequency_and_level(tones_dir):
    """Each annotated tone is the strongest bin near its frequency, at about its level.

    Averages Hann-windowed FFT power over the first 0.5 s, then sums +-3 bins around the
    peak and divides by 1.5 (the Hann window's noise-equivalent bandwidth in bins), which
    does not depend on where the tone falls between bins.
    """
    raw = np.fromfile(tones_dir / "tones_2m4.sigmf-data", dtype=np.uint8, count=synth_iq.RATE_SPS)
    meta = json.loads((tones_dir / "tones_2m4.sigmf-meta").read_text())
    x = (raw[0::2].astype(np.float64) - 127.5) / 127.5 + 1j * (raw[1::2].astype(np.float64) - 127.5) / 127.5
    window = np.hanning(N)
    blocks = len(x) // N
    power = np.zeros(N)
    for b in range(blocks):
        spectrum = np.fft.fftshift(np.fft.fft(x[b * N : (b + 1) * N] * window)) / window.sum()
        power += np.abs(spectrum) ** 2
    power /= blocks

    # The -60 dBFS tone is below one cu8 step and survives only through dither, so it gets 3 dB.
    tolerances_db = [1.0, 1.0, 3.0]
    for annotation, (_, level_dbfs), tol_db in zip(meta["annotations"], synth_iq.TONES, tolerances_db):
        centre_hz = (annotation["core:freq_lower_edge"] + annotation["core:freq_upper_edge"]) / 2
        offset_hz = centre_hz - meta["captures"][0]["core:frequency"]
        expected_bin = N // 2 + offset_hz / BIN_WIDTH_HZ
        lo = int(expected_bin) - 20
        peak_bin = int(np.argmax(power[lo : lo + 41])) + lo
        assert abs(peak_bin - expected_bin) <= 1.0
        tone_power = np.sum(power[peak_bin - 3 : peak_bin + 4]) / 1.5
        assert abs(10.0 * np.log10(tone_power) - level_dbfs) <= tol_db
