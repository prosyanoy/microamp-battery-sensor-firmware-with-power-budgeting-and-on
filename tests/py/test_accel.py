import numpy as np
import pytest

from microamp_sim import accel as A
from microamp_sim.scenarios import BASE_HZ, Trace


def flat_trace(g=(0.0, 0.0, 1.0), dur=10.0):
    acc = np.tile(np.array(g, dtype=float), (int(dur * BASE_HZ), 1))
    return Trace(dur, acc)


def dev(trace=None):
    return A.Accel(trace or flat_trace())


def wr(d, reg, val):
    d.transact(bytes([reg, val]), 0)


def rd(d, reg, n=1):
    return d.transact(bytes([reg | (0x80 if n > 1 else 0)]), n)[1]


def setup_stream(d, fth=16, odr=0x5):
    wr(d, A.CTRL4, 0x38)
    wr(d, A.CTRL5, 0x40)
    wr(d, A.FIFO_CTRL, (2 << 6) | fth)
    wr(d, A.CTRL1, (odr << 4) | 7)


def test_who_am_i_and_reset_defaults():
    d = dev()
    assert rd(d, A.WHO_AM_I) == b"\x33"
    assert rd(d, A.CTRL1) == b"\x07"


def test_read_only_registers_ignore_writes():
    d = dev()
    wr(d, A.WHO_AM_I, 0x00)
    assert rd(d, A.WHO_AM_I) == b"\x33"


def test_sample_conversion_one_g_hr_16g():
    d = dev()
    wr(d, A.CTRL4, 0x38)
    wr(d, A.CTRL1, 0x57)
    d.advance(20_000)
    raw = rd(d, A.OUT_X_L, 6)
    z = int.from_bytes(raw[4:6], "little", signed=True) >> 4
    assert z == round(1000 / 12)
    assert int.from_bytes(raw[0:2], "little", signed=True) >> 4 == 0


def test_odr_controls_sample_count():
    d = dev()
    setup_stream(d, odr=0x5)
    d.advance(200_000)
    assert d.samples_generated in (20, 21)


def test_fifo_level_and_watermark_follow_datasheet():
    d = dev()
    setup_stream(d, fth=16)
    wr(d, A.CTRL3, 0x04)  # route WTM to INT1
    d.advance(160_000 + 5_000)   # ~17 samples
    src = rd(d, A.FIFO_SRC)[0]
    assert src & 0x1F == 17 and src & 0x80          # exceeds watermark
    assert d.pin


def test_reading_out_regs_pops_oldest_first():
    acc = np.zeros((4000, 3))
    acc[:, 0] = np.arange(4000) * 0.01           # ramp so each sample differs
    d = dev(Trace(10.0, acc))
    setup_stream(d)
    d.advance(100_000)
    first = int.from_bytes(rd(d, A.OUT_X_L, 6)[0:2], "little", signed=True) >> 4
    second = int.from_bytes(rd(d, A.OUT_X_L, 6)[0:2], "little", signed=True) >> 4
    assert second > first
    assert len(d.fifo) == d.samples_generated - 2


def test_stream_mode_overwrites_oldest_and_flags_full():
    d = dev()
    setup_stream(d)
    d.advance(1_000_000)                            # 100 samples into a 32 deep FIFO
    src = rd(d, A.FIFO_SRC)[0]
    assert src & 0x40 and len(d.fifo) == 32


def test_fifo_mode_stops_when_full():
    d = dev()
    wr(d, A.CTRL5, 0x40)
    wr(d, A.FIFO_CTRL, (1 << 6) | 10)
    wr(d, A.CTRL1, 0x57)
    d.advance(1_000_000)
    assert len(d.fifo) == 32


def test_bypass_mode_uses_drdy_and_clears_on_read():
    d = dev()
    wr(d, A.CTRL1, 0x57)
    d.advance(25_000)
    assert rd(d, A.STATUS)[0] & 0x08
    rd(d, A.OUT_X_L, 6)
    assert not rd(d, A.STATUS)[0] & 0x08


def test_pin_requires_routing():
    d = dev()
    setup_stream(d)
    d.advance(400_000)
    assert not d.pin                                 # CTRL_REG3 not routed
    wr(d, A.CTRL3, 0x04)
    assert d.pin


def test_motion_interrupt_ignores_gravity_but_sees_a_bump():
    acc = np.tile([0.0, 0.0, 1.0], (4000, 1))
    acc[1500:1510, 0] += 1.0                         # 1 g bump at t=3.75 s
    d = dev(Trace(10.0, acc))
    setup_stream(d)
    wr(d, A.CTRL2, 0x01)
    wr(d, A.INT1_THS, 1)
    wr(d, A.INT1_CFG, 0x2A)
    wr(d, A.CTRL3, 0x40)
    d.advance(3_000_000)
    assert not d.pin                                 # 1 g static is filtered out
    d.advance(4_000_000)
    assert d.pin
    src = rd(d, A.INT1_SRC)[0]
    assert src & 0x40 and not d.pin                  # reading INT1_SRC releases the latch


def test_wait_stops_at_interrupt_time():
    acc = np.tile([0.0, 0.0, 1.0], (4000, 1))
    acc[2000:2010, 1] += 2.0                         # at t = 5.0 s
    d = dev(Trace(10.0, acc))
    setup_stream(d)
    wr(d, A.CTRL2, 0x01); wr(d, A.INT1_THS, 1); wr(d, A.INT1_CFG, 0x2A); wr(d, A.CTRL3, 0x40)
    t = d.advance(9_000_000, stop_on_pin=True)
    assert 4_990_000 <= t <= 5_030_000 and d.pin


def test_wait_times_out_without_event():
    d = dev()
    setup_stream(d)
    assert d.advance(2_000_000, stop_on_pin=True) == 2_000_000


def test_boot_bit_resets_registers():
    d = dev()
    wr(d, A.CTRL4, 0x38)
    wr(d, A.CTRL5, 0x80)
    assert rd(d, A.CTRL4) == b"\x00"


def test_range_clipping():
    acc = np.tile([30.0, 0.0, 0.0], (400, 1))
    d = dev(Trace(1.0, acc))
    wr(d, A.CTRL4, 0x38)
    wr(d, A.CTRL1, 0x57)
    d.advance(30_000)
    x = int.from_bytes(rd(d, A.OUT_X_L, 6)[0:2], "little", signed=True) >> 4
    assert x == 16000 // 12 - 1
