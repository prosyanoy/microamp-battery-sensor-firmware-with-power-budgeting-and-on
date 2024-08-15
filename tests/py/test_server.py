import io
import struct
import threading

import numpy as np

from microamp_sim.accel import I2C_ADDR, Accel
from microamp_sim.scenarios import BASE_HZ, Trace
from microamp_sim.server import serve


def run(requests: bytes, trace=None) -> bytes:
    trace = trace or Trace(5.0, np.tile([0, 0, 1.0], (5 * BASE_HZ, 1)))
    rd, out = io.BytesIO(requests), io.BytesIO()
    serve(Accel(trace), rd, out)
    return out.getvalue()


def t_req(addr, w, rlen, now):
    return b"T" + struct.pack("<BBBQ", addr, len(w), rlen, now) + bytes(w)


def test_transaction_reads_who_am_i():
    out = run(t_req(I2C_ADDR, [0x0F], 1, 0))
    assert out == bytes([1, 0, 0x33])


def test_wrong_address_is_nacked():
    out = run(t_req(0x50, [0x0F], 2, 0))
    assert out[0] == 0 and len(out) == 4


def test_wait_returns_timeout_time_when_idle():
    reqs = t_req(I2C_ADDR, [0x20, 0x57], 0, 0) + b"W" + struct.pack("<QQ", 1000, 500_000)
    out = run(reqs)
    reached, irq = struct.unpack("<QB", out[2:])
    assert reached == 501_000 and irq == 0


def test_pin_poll_and_clean_eof():
    out = run(b"P" + struct.pack("<Q", 10))
    assert out == b"\x00"


def test_truncated_request_terminates():
    assert run(b"T\x18") == b""
