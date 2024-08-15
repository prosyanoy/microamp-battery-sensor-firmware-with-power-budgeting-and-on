import numpy as np

from microamp_sim import scenarios as sc


def test_trace_is_deterministic_per_seed():
    a, b = sc.build_trace("day", 60, 3), sc.build_trace("day", 60, 3)
    assert np.array_equal(a.acc, b.acc)
    assert not np.array_equal(a.acc, sc.build_trace("day", 60, 4).acc)


def test_resting_trace_is_about_one_g_with_low_noise():
    t = sc.build_trace("parked", 30, 1)
    mag = np.linalg.norm(t.acc[:2000], axis=1)
    assert abs(mag.mean() - 1.0) < 0.01 and mag.std() < 0.02


def test_events_inside_duration_and_separated():
    t = sc.build_trace("day", 600, 7)
    assert t.events
    ev = sorted(t.events, key=lambda e: e.t_start)
    for e in ev:
        assert 0 <= e.t_start < e.t_end <= 600.5
    assert all(b.t_start - a.t_end > 1.0 for a, b in zip(ev, ev[1:]))


def test_brake_pulls_forward_axis_negative():
    t = sc.build_trace("drive", 120, 5)
    brakes = [e for e in t.events if e.cls == "harsh_brake"]
    assert brakes
    e = brakes[0]
    mid = t.acc[int((e.t_start + e.t_end) / 2 * sc.BASE_HZ)]
    assert mid[0] < t.acc[:200, 0].mean() - 0.3


def test_impact_peak_is_large():
    t = sc.build_trace("parked", 300, 7)
    imp = [e for e in t.events if e.cls == "impact"]
    assert imp
    seg = t.acc[int(imp[0].t_start * sc.BASE_HZ):int(imp[0].t_end * sc.BASE_HZ) + 1]
    assert np.abs(seg).max() > 3.0


def test_quantise_mg_clips_and_snaps():
    q = sc.quantise_mg(np.array([[0.0, 1.0, 40.0]]))
    assert q[0, 0] == 0 and q[0, 1] == 996 and q[0, 2] == 15984
    assert (q % 12 == 0).all()
