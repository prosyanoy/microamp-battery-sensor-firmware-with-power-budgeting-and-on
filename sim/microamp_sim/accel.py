"""LIS3DH-style 3-axis accelerometer: register map, 32-deep FIFO, INT1 pin.

Implemented registers (subset of the ST LIS3DH, same addresses/bit layout):
  0x0F WHO_AM_I (0x33)      0x20 CTRL_REG1 (ODR[7:4], LPen, XYZ enable)
  0x21 CTRL_REG2 (HPIS1)    0x22 CTRL_REG3 (INT1 routing)
  0x23 CTRL_REG4 (FS, HR)   0x24 CTRL_REG5 (BOOT, FIFO_EN)
  0x27 STATUS_REG           0x28..0x2D OUT_X/Y/Z L/H
  0x2E FIFO_CTRL            0x2F FIFO_SRC
  0x30 INT1_CFG  0x31 INT1_SRC  0x32 INT1_THS  0x33 INT1_DURATION
Reading OUT_Z_H pops one sample from the FIFO (when FIFO is enabled), exactly
like the real part, so a driver must read 6 bytes per sample.
"""
from __future__ import annotations

from collections import deque

from .scenarios import Trace

WHO_AM_I, CTRL1, CTRL2, CTRL3, CTRL4, CTRL5 = 0x0F, 0x20, 0x21, 0x22, 0x23, 0x24
STATUS, OUT_X_L, OUT_Z_H = 0x27, 0x28, 0x2D
FIFO_CTRL, FIFO_SRC = 0x2E, 0x2F
INT1_CFG, INT1_SRC, INT1_THS, INT1_DUR = 0x30, 0x31, 0x32, 0x33

ODR_HZ = {0: 0, 1: 1, 2: 10, 3: 25, 4: 50, 5: 100, 6: 200, 7: 400}
FS_G = {0: 2, 1: 4, 2: 8, 3: 16}
HR_MG_PER_LSB = {0: 1, 1: 2, 2: 4, 3: 12}
INT_THS_MG = {0: 16, 1: 32, 2: 62, 3: 186}
FIFO_DEPTH = 32
HP_ALPHA = 0.05
I2C_ADDR = 0x18


class Accel:
    def __init__(self, trace: Trace):
        self.trace = trace
        self.now_us = 0
        self.reset()

    def reset(self):
        self.reg = {a: 0 for a in range(0x00, 0x40)}
        self.reg[WHO_AM_I] = 0x33
        self.reg[CTRL1] = 0x07
        self.fifo: deque = deque()
        self.out = (0, 0, 0)
        self.zyxda = False
        self.ia1_latched = False
        self.ia1_src = 0
        self.next_sample_us = None
        self.lp = [0.0, 0.0, 0.0]
        self.lp_init = False
        self.samples_generated = 0

    # ----- configuration helpers
    @property
    def odr(self) -> int:
        return ODR_HZ.get((self.reg[CTRL1] >> 4) & 0xF, 0)

    @property
    def fs_code(self) -> int:
        return (self.reg[CTRL4] >> 4) & 3

    @property
    def fifo_enabled(self) -> bool:
        return bool(self.reg[CTRL5] & 0x40)

    @property
    def fifo_mode(self) -> int:
        return (self.reg[FIFO_CTRL] >> 6) & 3

    @property
    def fth(self) -> int:
        return self.reg[FIFO_CTRL] & 0x1F

    # ----- register access
    def read_reg(self, addr: int) -> int:
        if addr == STATUS:
            return 0x08 if self.zyxda else 0
        if OUT_X_L <= addr <= OUT_Z_H:
            idx = addr - OUT_X_L
            v = self._out_word(idx // 2)
            b = (v >> (8 * (idx % 2))) & 0xFF
            if addr == OUT_Z_H:
                self._consume_sample()
            return b
        if addr == FIFO_SRC:
            n = len(self.fifo) if self.fifo_enabled else 0
            return (0x80 if n > self.fth else 0) | (0x40 if n >= FIFO_DEPTH else 0) \
                | (0x20 if n == 0 else 0) | (n & 0x1F)
        if addr == INT1_SRC:
            v = self.ia1_src
            self.ia1_latched = False
            self.ia1_src = 0
            return v
        return self.reg.get(addr, 0)

    def write_reg(self, addr: int, val: int):
        if addr in (WHO_AM_I, STATUS, FIFO_SRC, INT1_SRC) or OUT_X_L <= addr <= OUT_Z_H:
            return
        old = self.reg.get(addr, 0)
        self.reg[addr] = val & 0xFF
        if addr == CTRL5 and val & 0x80:               # BOOT: reload defaults
            self.reset()
            return
        if addr in (CTRL1, FIFO_CTRL, CTRL5) and (old != val):
            if addr == CTRL1:
                self.next_sample_us = self.now_us if self.odr else None
                self.lp_init = False
            if addr in (FIFO_CTRL, CTRL5):
                self.fifo.clear()                      # mode change resets FIFO

    # ----- sample path
    def _out_word(self, axis: int) -> int:
        if self.fifo_enabled and self.fifo_mode != 0:   # OUT regs show FIFO head
            return (self.fifo[0] if self.fifo else (0, 0, 0))[axis] & 0xFFFF
        return self.out[axis] & 0xFFFF

    def _consume_sample(self):
        self.zyxda = False
        if self.fifo_enabled and self.fifo_mode != 0 and self.fifo:
            self.fifo.popleft()

    def _quantise(self, g3):
        hr = bool(self.reg[CTRL4] & 0x08)
        mg_lsb = HR_MG_PER_LSB[self.fs_code] if hr else HR_MG_PER_LSB[self.fs_code] * 4
        lim = FS_G[self.fs_code] * 1000 // mg_lsb
        words = []
        for a in g3:
            c = int(round(a * 1000.0 / mg_lsb))
            c = max(-lim, min(lim - 1, c))
            words.append((c << 4) & 0xFFFF if hr else (c << 6) & 0xFFFF)
        return tuple(words)

    def _generate(self, t_us: int):
        g3 = self.trace.at(t_us / 1e6)
        sample = self._quantise(g3)
        self.samples_generated += 1
        self.out = sample
        self.zyxda = True
        if self.fifo_enabled and self.fifo_mode != 0:
            if len(self.fifo) < FIFO_DEPTH:
                self.fifo.append(sample)
            elif self.fifo_mode >= 2:                  # stream: overwrite oldest
                self.fifo.popleft()
                self.fifo.append(sample)
        self._check_motion(g3)

    def _check_motion(self, g3):
        if not self.lp_init:
            self.lp = [float(v) for v in g3]
            self.lp_init = True
        hp = [float(v) - l for v, l in zip(g3, self.lp)]
        self.lp = [l + HP_ALPHA * (float(v) - l) for v, l in zip(g3, self.lp)]
        src = hp if self.reg[CTRL2] & 0x01 else [float(v) for v in g3]
        cfg = self.reg[INT1_CFG]
        ths = self.reg[INT1_THS] * INT_THS_MG[self.fs_code] / 1000.0
        bits = 0
        for i, (hi_bit, lo_bit) in enumerate(((0x02, 0x01), (0x08, 0x04), (0x20, 0x10))):
            if (cfg & hi_bit) and src[i] > ths:
                bits |= hi_bit
            if (cfg & lo_bit) and src[i] < -ths:
                bits |= lo_bit
        if bits:
            self.ia1_latched = True
            self.ia1_src = 0x40 | bits

    # ----- time
    @property
    def pin(self) -> bool:
        c3 = self.reg[CTRL3]
        n = len(self.fifo) if self.fifo_enabled else 0
        return bool(
            ((c3 & 0x40) and self.ia1_latched)
            or ((c3 & 0x10) and self.zyxda)
            or ((c3 & 0x04) and self.fifo_enabled and n > self.fth)
            or ((c3 & 0x02) and self.fifo_enabled and n >= FIFO_DEPTH)
        )

    def advance(self, now_us: int, stop_on_pin: bool = False) -> int:
        """Run sample generation up to now_us. Returns the time actually reached
        (earlier than now_us if stop_on_pin and the INT pin asserted)."""
        if now_us <= self.now_us:
            return self.now_us
        odr = self.odr
        if odr and self.next_sample_us is not None:
            period = 1_000_000 // odr
            while self.next_sample_us <= now_us:
                t = self.next_sample_us
                self._generate(t)
                self.next_sample_us += period
                if stop_on_pin and self.pin:
                    self.now_us = max(self.now_us, t)
                    return self.now_us
        self.now_us = now_us
        return now_us

    # ----- I2C transaction
    def transact(self, wdata: bytes, rlen: int):
        """Returns (ack, rdata). First write byte is the sub-address."""
        ptr, inc = 0, True
        if wdata:
            ptr, inc = wdata[0] & 0x7F, bool(wdata[0] & 0x80)
            for b in wdata[1:]:
                self.write_reg(ptr, b)
                ptr += 1 if inc else 0
        out = bytearray()
        for _ in range(rlen):
            out.append(self.read_reg(ptr))
            ptr += 1 if inc else 0
        return True, bytes(out)
