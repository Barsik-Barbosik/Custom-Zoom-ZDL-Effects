"""Shared helpers for the SongBox instruments (instruments/*.py) and proto.py.

DSP-legal per sample: multiply / add, parabolic sine, no division or libm
(coef() / lp_coef() run once, as the C constants in songbox_instr.h).
"""

from __future__ import annotations

import math

SR = 44100
WT_BITS = 11
WT_N = 1 << WT_BITS   # wavetable words per cycle (instruments with TABLE; + 1 guard word)
WT_OFS = 6464.0    # a word's high half + this: the word reads as a tiny float (silent)


def psin(p: float) -> float:
    """sin(2 pi p), p in [0, 1): parabolic approximation (as db_sin)."""
    x = p * 2.0 - 1.0                              # -1..1
    y = 4.0 * x * (1.0 - (x if x >= 0.0 else -x))
    y = 0.225 * (y * (y if y >= 0.0 else -y) - y) + y
    return -y


def wrap(p: float) -> float:
    return p - int(p) if p >= 0.0 else p - int(p) + 1.0


def tri(v, inc: float) -> float:
    """Triangle LFO in v.p3 (inc = Hz / SR), -1..1 (as ss_tri)."""
    v.p3 = wrap(v.p3 + inc)
    x = v.p3 - 0.5
    return 4.0 * (x if x >= 0.0 else -x) - 1.0


def clip(x: float) -> float:
    """Cubic soft clip of x clamped to -1..1 (as ss_clip)."""
    y = abs(x + 1.0) - abs(x - 1.0)                 # 2 clamp1(x) (C: branch-free)
    return y * (0.75 - 0.0625 * y * y)


def rise(ms: float) -> float:
    """Attack coefficient: env += (1 - env) * rise(ms)."""
    return 1.0 - coef(ms)


def coef(ms: float) -> float:
    """Per-sample decay factor reaching -60 dB after ms."""
    return 10.0 ** (-3.0 / (ms * 0.001 * SR))


def lp_coef(hz: float) -> float:
    return 1.0 - math.exp(-2.0 * math.pi * hz / SR)


class Voice:
    """One voice (songbox.c SsVoice): phase increment f per sample, gate in
    samples (counts down), state the instruments share."""

    def __init__(self):
        self.on = False
        self.gate = 0
        self.f = 0.0
        self.p = self.p2 = self.p3 = 0.0
        self.ip = self.ip2 = self.ip3 = 0          # integer phases (2^32 = a cycle)
        self.env = 0.0
        self.menv = 0.0
        self.lp = 0.0
        self.lp2 = 0.0
        self.bp = 0.0
        self.att = 1.0

    def trigger(self, f: float, length: int):
        self.on = True
        self.f = f * (1.0 / SR)
        self.gate = length
        self.menv = 1.0
        self.att = 0.0
