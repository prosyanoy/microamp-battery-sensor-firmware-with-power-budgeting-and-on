"""Integer-exact feature extraction; mirrors firmware/ml/features.c bit for bit."""
from __future__ import annotations

from math import isqrt

import numpy as np

WINDOW = 64
N_FEATURES = 16
FEATURE_NAMES = (
    ["mean_x", "mean_y", "mean_z", "std_x", "std_y", "std_z",
     "maxabs_x", "maxabs_y", "maxabs_z", "jerk_x", "jerk_y", "jerk_z",
     "mag_peak", "mag_mean", "mag_rms", "ptp_sum"]
)


def extract(window_mg, baseline_mg) -> list[int]:
    """window_mg: (64,3) ints in milli-g; baseline_mg: 3 ints. Returns 16 ints."""
    d = np.asarray(window_mg, dtype=np.int64) - np.asarray(baseline_mg, dtype=np.int64)
    n = len(d)
    assert n == WINDOW
    mean = d.sum(axis=0) >> 6
    sumsq = (d * d).sum(axis=0)
    var = np.maximum((sumsq >> 6) - mean * mean, 0)
    std = [isqrt(int(v)) for v in var]
    maxabs = np.abs(d).max(axis=0)
    jerk = np.abs(np.diff(d, axis=0)).sum(axis=0) // (n - 1)
    sq = (d * d).sum(axis=1)
    mags = [isqrt(int(v)) for v in sq]
    mag_rms = isqrt(int(sq.sum()) >> 6)
    ptp = int((d.max(axis=0) - d.min(axis=0)).sum())
    f = [*map(int, mean), *std, *map(int, maxabs), *map(int, jerk),
         max(mags), sum(mags) >> 6, mag_rms, ptp]
    return f


def quantise(features, mult, shift: int = 12) -> list[int]:
    """int8 input vector: clamp((f*M + round) >> shift)."""
    out = []
    for f, m in zip(features, mult):
        f = max(-32768, min(32767, int(f)))
        q = (f * int(m) + (1 << (shift - 1))) >> shift
        out.append(max(-128, min(127, q)))
    return out
