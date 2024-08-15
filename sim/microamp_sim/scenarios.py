"""Synthetic motion traces with ground-truth events.

The same generator feeds the simulated accelerometer and the training-set
builder so that the model is trained on the distribution it will see on-device.
Device frame: x = forward, y = left, z = up. Units are g.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

BASE_HZ = 400  # every supported ODR divides this


@dataclass
class TruthEvent:
    cls: str          # "harsh_brake" | "impact"
    t_start: float    # seconds
    t_end: float


@dataclass
class Trace:
    duration_s: float
    acc: np.ndarray                       # (N, 3) float64 in g, at BASE_HZ
    events: list[TruthEvent] = field(default_factory=list)
    segments: list[tuple[str, float, float]] = field(default_factory=list)

    def at(self, t: float) -> np.ndarray:
        i = int(round(t * BASE_HZ))
        return self.acc[min(i, len(self.acc) - 1)]


def _lowpass(x: np.ndarray, tau_samples: float) -> np.ndarray:
    k = np.exp(-np.arange(int(tau_samples * 6) + 1) / tau_samples)
    k /= k.sum()
    out = np.empty_like(x)
    for a in range(x.shape[1]):
        out[:, a] = np.convolve(x[:, a], k, mode="full")[: len(x)]
    return out


def gravity_vector(rng: np.random.Generator, max_tilt_deg: float = 10.0) -> np.ndarray:
    th = np.deg2rad(rng.uniform(0, max_tilt_deg))
    ph = rng.uniform(0, 2 * np.pi)
    return np.array([np.sin(th) * np.cos(ph), np.sin(th) * np.sin(ph), np.cos(th)])


def add_brake(acc, rng, t0, fs=BASE_HZ, strength=None, dur=None):
    strength = strength if strength is not None else rng.uniform(0.5, 1.0)
    dur = dur if dur is not None else rng.uniform(0.7, 1.6)
    n = int(dur * fs)
    ramp = max(int(0.12 * fs), 2)
    env = np.ones(n)
    env[:ramp] = np.linspace(0, 1, ramp)
    env[-ramp:] = np.linspace(1, 0, ramp)
    env *= 1 + 0.08 * np.sin(2 * np.pi * rng.uniform(1.5, 4) * np.arange(n) / fs)
    i0 = int(t0 * fs)
    i1 = min(i0 + n, len(acc))
    m = i1 - i0
    acc[i0:i1, 0] -= strength * env[:m]
    acc[i0:i1, 2] += 0.12 * strength * env[:m]   # nose dive
    return i0 / fs, i1 / fs


def add_impact(acc, rng, t0, fs=BASE_HZ, peak=None, dur=None):
    peak = peak if peak is not None else rng.uniform(4.0, 14.0)
    dur = dur if dur is not None else rng.uniform(0.12, 0.30)
    n = int(dur * fs)
    tt = np.arange(n) / fs
    f = rng.uniform(18, 45)
    ring = np.exp(-tt / (dur / 4)) * np.sin(2 * np.pi * f * tt + rng.uniform(0, 1))
    direction = rng.normal(size=3)
    direction /= np.linalg.norm(direction)
    i0 = int(t0 * fs)
    i1 = min(i0 + n, len(acc))
    acc[i0:i1] += peak * ring[: i1 - i0, None] * direction[None, :]
    return i0 / fs, i1 / fs


def add_bump(acc, rng, t0, fs=BASE_HZ):
    """Speed bump / pothole: benign, mostly vertical."""
    peak = rng.uniform(0.4, 1.8)
    n = int(rng.uniform(0.08, 0.25) * fs)
    i0 = int(t0 * fs)
    i1 = min(i0 + n, len(acc))
    acc[i0:i1, 2] += peak * np.sin(np.pi * np.arange(i1 - i0) / n)
    acc[i0:i1, 0] += rng.uniform(-0.15, 0.15) * np.sin(np.pi * np.arange(i1 - i0) / n)


def add_cruise(acc, rng, fs=BASE_HZ):
    n = len(acc)
    vib = _lowpass(rng.normal(size=(n, 3)), 3) * np.array([0.12, 0.12, 0.2])
    acc += vib * rng.uniform(0.6, 1.4)
    t = np.arange(n) / fs
    for ax, amp in ((0, 0.20), (1, 0.28)):
        for _ in range(3):
            acc[:, ax] += rng.uniform(0, amp / 2) * np.sin(
                2 * np.pi * t / rng.uniform(4, 15) + rng.uniform(0, 6.3))


def _segment_plan(kind: str, duration: float, rng) -> list[tuple[str, float]]:
    if kind == "parked":
        return [("parked", duration)]
    if kind == "drive":
        return [("drive", duration)]
    plan, left, cur = [], duration, "parked"
    while left > 0:
        ln = min(left, rng.uniform(0.12, 0.28) * duration) if cur == "parked" \
            else min(left, rng.uniform(0.1, 0.22) * duration)
        plan.append((cur, ln))
        left -= ln
        cur = "drive" if cur == "parked" else "parked"
    return plan


def build_trace(kind: str = "day", duration_s: float = 600.0, seed: int = 7,
                noise_g: float = 0.004) -> Trace:
    rng = np.random.default_rng(seed)
    n = int(duration_s * BASE_HZ)
    g = gravity_vector(rng)
    acc = np.tile(g, (n, 1)).astype(np.float64)
    acc += rng.normal(scale=noise_g, size=acc.shape)
    trace = Trace(duration_s, acc)
    t = 0.0
    for seg, ln in _segment_plan(kind, duration_s, rng):
        i0, i1 = int(t * BASE_HZ), int((t + ln) * BASE_HZ)
        trace.segments.append((seg, t, t + ln))
        if seg == "drive":
            sub = acc[i0:i1]
            add_cruise(sub, rng)
            tt = t + 2.0
            while tt < t + ln - 4:                   # benign bumps
                add_bump(acc, rng, tt)
                tt += rng.exponential(12.0)
            tt = t + rng.uniform(4, 12)              # harsh braking
            while tt < t + ln - 4:
                a, b = add_brake(acc, rng, tt)
                trace.events.append(TruthEvent("harsh_brake", a, b))
                tt = b + rng.uniform(25, 70)
        t += ln
    # impacts anywhere (parked cars get bumped too), well separated
    tt = rng.uniform(10, 40)
    while tt < duration_s - 3:
        if all(abs(tt - e.t_start) > 4 and abs(tt - e.t_end) > 4 for e in trace.events):
            a, b = add_impact(acc, rng, tt)
            trace.events.append(TruthEvent("impact", a, b))
        tt += rng.uniform(60, 150)
    trace.events.sort(key=lambda e: e.t_start)
    return trace


def quantise_mg(g3: np.ndarray, mg_per_lsb: int = 12, fs_g: int = 16) -> np.ndarray:
    """What the sensor (16 g, 12-bit high-res) + driver turn an acceleration
    in g into: integer milli-g in multiples of the LSB, clipped to full scale."""
    lim = fs_g * 1000 // mg_per_lsb
    c = np.clip(np.rint(g3 * 1000.0 / mg_per_lsb), -lim, lim - 1).astype(np.int64)
    return c * mg_per_lsb
