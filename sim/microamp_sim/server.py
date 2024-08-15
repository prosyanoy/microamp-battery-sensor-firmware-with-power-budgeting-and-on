"""Binary request/response bridge between the firmware (HAL) and the simulator.

Requests (little endian), all carry the firmware's virtual time:
  'T' u8 addr, u8 wlen, u8 rlen, u64 now_us, wdata[wlen]
        -> u8 ack, u8 irq, rdata[rlen]
  'W' u64 now_us, u64 max_us          (wait for INT pin or timeout; tickless sleep)
        -> u64 reached_us, u8 irq
  'P' u64 now_us                      (read the INT pin)  -> u8 irq
EOF on stdin terminates the simulator.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

from .accel import I2C_ADDR, Accel
from .scenarios import build_trace


def _read_exact(f, n: int) -> bytes:
    buf = f.read(n)
    if len(buf) != n:
        raise EOFError
    return buf


def serve(dev: Accel, rd, wr) -> None:
    while True:
        try:
            cmd = _read_exact(rd, 1)
        except EOFError:
            return
        try:
            if cmd == b"T":
                addr, wlen, rlen, now = struct.unpack("<BBBQ", _read_exact(rd, 11))
                wdata = _read_exact(rd, wlen)
                dev.advance(now)
                if addr != I2C_ADDR:
                    wr.write(struct.pack("<BB", 0, int(dev.pin)) + bytes(rlen))
                else:
                    ack, data = dev.transact(wdata, rlen)
                    wr.write(struct.pack("<BB", int(ack), int(dev.pin)) + data)
            elif cmd == b"W":
                now, max_us = struct.unpack("<QQ", _read_exact(rd, 16))
                dev.advance(now)
                reached = now if dev.pin else dev.advance(now + max_us, stop_on_pin=True)
                wr.write(struct.pack("<QB", reached, int(dev.pin)))
            elif cmd == b"P":
                (now,) = struct.unpack("<Q", _read_exact(rd, 8))
                dev.advance(now)
                wr.write(struct.pack("<B", int(dev.pin)))
            else:
                return
        except EOFError:
            return
        wr.flush()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--scenario", default="day", choices=["day", "drive", "parked"])
    ap.add_argument("--duration", type=float, default=600.0)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--truth", help="write ground-truth events (JSON) to this path")
    a = ap.parse_args(argv)
    trace = build_trace(a.scenario, a.duration, a.seed)
    if a.truth:
        with open(a.truth, "w") as f:
            json.dump({"scenario": a.scenario, "seed": a.seed, "duration_s": a.duration,
                       "events": [e.__dict__ for e in trace.events]}, f, indent=1)
    serve(Accel(trace), sys.stdin.buffer, sys.stdout.buffer)
    return 0
