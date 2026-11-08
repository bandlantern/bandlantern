"""Synthetic IQ generator (docs/api/testdata.md, "Synthetic scenarios").

Writes <out>/<stem>.sigmf-meta and <stem>.sigmf-data (SigMF core v1.2, cu8).
Deterministic: same seed -> identical bytes.

Usage: .venv/bin/python tools/py/synth_iq.py --out testdata/synth
"""

import argparse
import json
from pathlib import Path

import numpy as np

RATE_SPS = 2_400_000
CENTER_FREQ_HZ = 100_000_000
NOISE_DBFS = -70.0  # total complex noise power
ANNOTATION_HALF_WIDTH_HZ = 500
CHUNK_SAMPLES = 1 << 20

# (frequency offset from centre in Hz, level in dBFS)
TONES = [(150_000, -20.0), (-400_000, -40.0), (900_000, -60.0)]

SWEEP_START_HZ = -1_000_000
SWEEP_STOP_HZ = 1_000_000
SWEEP_LEVEL_DBFS = -20.0


def dbfs_to_amplitude(level_dbfs):
    """A full-scale complex tone (|x| = 1) is 0 dBFS (docs/api/frames.md)."""
    return 10.0 ** (level_dbfs / 20.0)


def to_cu8(x):
    """Inverse of (b - 127.5) / 127.5, rounded and clipped to 0..255; interleaved I,Q."""
    out = np.empty(2 * len(x), dtype=np.uint8)
    out[0::2] = np.clip(np.round(x.real * 127.5 + 127.5), 0, 255).astype(np.uint8)
    out[1::2] = np.clip(np.round(x.imag * 127.5 + 127.5), 0, 255).astype(np.uint8)
    return out


def tones_chunk(start, count, phases_rad):
    n = np.arange(start, start + count, dtype=np.float64)
    x = np.zeros(count, dtype=np.complex128)
    for (freq_hz, level_dbfs), phase in zip(TONES, phases_rad):
        # Reduce to one cycle first to keep float64 precision at large n.
        cycles = np.mod(freq_hz * n / RATE_SPS, 1.0)
        x += dbfs_to_amplitude(level_dbfs) * np.exp(1j * (2.0 * np.pi * cycles + phase))
    return x


def sweep_chunk(start, count, total_samples):
    # Linear chirp: f(t) = f0 + (f1 - f0) t / T, phase = 2 pi (f0 t + (f1 - f0) t^2 / (2T)).
    t = np.arange(start, start + count, dtype=np.float64) / RATE_SPS
    duration_s = total_samples / RATE_SPS
    rate_hz_per_s = (SWEEP_STOP_HZ - SWEEP_START_HZ) / duration_s
    phase = 2.0 * np.pi * (SWEEP_START_HZ * t + 0.5 * rate_hz_per_s * t * t)
    return dbfs_to_amplitude(SWEEP_LEVEL_DBFS) * np.exp(1j * phase)


def noise_chunk(rng, count):
    # Total complex power 10^(NOISE_DBFS/10) split equally between I and Q.
    sigma = np.sqrt(10.0 ** (NOISE_DBFS / 10.0) / 2.0)
    return sigma * (rng.standard_normal(count) + 1j * rng.standard_normal(count))


def tone_annotation(freq_offset_hz, label):
    center = CENTER_FREQ_HZ + freq_offset_hz
    return {
        "core:sample_start": 0,
        "core:freq_lower_edge": center - ANNOTATION_HALF_WIDTH_HZ,
        "core:freq_upper_edge": center + ANNOTATION_HALF_WIDTH_HZ,
        "core:label": label,
    }


def meta(description, annotations):
    return {
        "global": {
            "core:datatype": "cu8",
            "core:sample_rate": RATE_SPS,
            "core:version": "1.2.0",
            "core:description": description,
            "core:recorder": "bandlantern synth_iq.py",
        },
        "captures": [{"core:sample_start": 0, "core:frequency": CENTER_FREQ_HZ}],
        "annotations": annotations,
    }


SCENARIOS = {
    "tones_2m4": {
        "seed": 1,
        "duration_s": 2,
        "meta": meta(
            "Three tones + noise, seed 1",
            [tone_annotation(f, f"tone {int(db)} dBFS") for f, db in TONES],
        ),
    },
    "sweep_2m4": {
        "seed": 2,
        "duration_s": 4,
        "meta": meta(
            "Linear sweep -1.0 to +1.0 MHz at -20 dBFS + noise, seed 2",
            [
                {
                    "core:sample_start": 0,
                    "core:freq_lower_edge": CENTER_FREQ_HZ + SWEEP_START_HZ,
                    "core:freq_upper_edge": CENTER_FREQ_HZ + SWEEP_STOP_HZ,
                    "core:label": "sweep -20 dBFS",
                }
            ],
        ),
    },
}


def scenario_cu8(stem):
    """Yield cu8 byte chunks for a scenario."""
    spec = SCENARIOS[stem]
    rng = np.random.default_rng(spec["seed"])
    total = spec["duration_s"] * RATE_SPS
    phases_rad = rng.uniform(0.0, 2.0 * np.pi, len(TONES)) if stem == "tones_2m4" else None
    for start in range(0, total, CHUNK_SAMPLES):
        count = min(CHUNK_SAMPLES, total - start)
        if stem == "tones_2m4":
            x = tones_chunk(start, count, phases_rad)
        else:
            x = sweep_chunk(start, count, total)
        yield to_cu8(x + noise_chunk(rng, count)).tobytes()


def write_scenario(stem, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    meta_text = json.dumps(SCENARIOS[stem]["meta"], indent=2) + "\n"
    (out_dir / f"{stem}.sigmf-meta").write_text(meta_text)
    with open(out_dir / f"{stem}.sigmf-data", "wb") as f:
        for chunk in scenario_cu8(stem):
            f.write(chunk)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="output directory")
    args = parser.parse_args()
    for stem in SCENARIOS:
        write_scenario(stem, args.out)
        print(f"wrote {Path(args.out) / stem}.sigmf-{{meta,data}}")


if __name__ == "__main__":
    main()
