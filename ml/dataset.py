"""Labelled training windows built from the same synthetic motion generator."""
from __future__ import annotations

import numpy as np

from microamp_sim import scenarios as sc
from .features import WINDOW, extract

CLASSES = ["normal", "harsh_brake", "impact"]
ODR = 100
DECIM = sc.BASE_HZ // ODR


def training_trace(rng: np.random.Generator, dur: float = 240.0) -> sc.Trace:
    n = int(dur * sc.BASE_HZ)
    g = sc.gravity_vector(rng, 12.0)
    acc = np.tile(g, (n, 1)) + rng.normal(scale=0.004, size=(n, 3))
    tr = sc.Trace(dur, acc)
    # a mix of parked and driving stretches
    t = 0.0
    while t < dur:
        ln = min(dur - t, rng.uniform(20, 60))
        if rng.random() < 0.65:
            i0, i1 = int(t * sc.BASE_HZ), int((t + ln) * sc.BASE_HZ)
            sc.add_cruise(acc[i0:i1], rng)
            tb = t + 1
            while tb < t + ln - 1:
                sc.add_bump(acc, rng, tb)
                tb += rng.exponential(5.0)
        t += ln
    t = rng.uniform(3, 8)
    while t < dur - 4:
        if rng.random() < 0.55:
            a, b = sc.add_brake(acc, rng, t)
            tr.events.append(sc.TruthEvent("harsh_brake", a, b))
            t = b + rng.uniform(2.5, 8)
        else:
            a, b = sc.add_impact(acc, rng, t)
            tr.events.append(sc.TruthEvent("impact", a, b))
            t = b + rng.uniform(2.5, 8)
    return tr


def windows_from_trace(tr: sc.Trace, rng, stride: int = 8):
    mg = sc.quantise_mg(tr.acc[::DECIM])
    grav = np.median(tr.acc[::DECIM][:200], axis=0) * 1000  # approx. baseline
    X, y = [], []
    for s in range(0, len(mg) - WINDOW, stride):
        t0, t1 = s / ODR, (s + WINDOW) / ODR
        label, skip = 0, False
        for e in tr.events:
            ov = max(0.0, min(t1, e.t_end) - max(t0, e.t_start))
            frac = ov / min(e.t_end - e.t_start, WINDOW / ODR)
            if frac >= 0.5:
                label = 1 if e.cls == "harsh_brake" else 2
            elif frac >= 0.1:
                skip = True
        if skip:
            continue
        base = np.rint(grav + rng.normal(scale=15, size=3)).astype(int)
        X.append(extract(mg[s:s + WINDOW], base))
        y.append(label)
    return np.array(X, dtype=np.int64), np.array(y, dtype=np.int64)


def build(seeds, dur: float = 240.0):
    Xs, ys = [], []
    for sd in seeds:
        rng = np.random.default_rng(1000 + sd)
        X, y = windows_from_trace(training_trace(rng, dur), rng)
        Xs.append(X)
        ys.append(y)
    return np.concatenate(Xs), np.concatenate(ys)
