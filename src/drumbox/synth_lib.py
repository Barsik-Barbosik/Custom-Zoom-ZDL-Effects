"""Shared helpers for the synth drums (drums/*.py) and preview.py.

Python mirrors of the inline helpers in drumbox.c (db_sin, db_wrap, db_frac,
db_sq, db_lerp, db_soft_clip, db_noise, db_metal_sq); keep them in sync.
"""

from __future__ import annotations

SR = 44100.0
INV_SR = 1.0 / SR
# Six detuned squares (metallic hat tone), db_metal_inc in drumbox.c.
METAL_HZ = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)


def lerp(a, b, t):
    return a + (b - a) * t


def db_sin(p):
    x = p * 2.0 - 1.0
    y = 4.0 * x * (1.0 - abs(x))
    return y + 0.225 * (y * abs(y) - y)


def wrap(p):
    return p - 1.0 if p >= 1.0 else p


def frac(p):
    f = p - int(p)          # int() truncates toward zero, like the C cast
    return f + 1.0 if f < 0.0 else f


def sq(p):
    return 1.0 if p < 0.5 else -1.0


def soft_clip(x):
    if x > 1.5:
        return 1.0
    if x < -1.5:
        return -1.0
    return x - 0.14814815 * x * x * x


def xorshift(x):
    """One xorshift32 step: (new state, sample in -1..1), as db_xorshift."""
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    return x, (x - 0x100000000 if x >= 0x80000000 else x) * 4.6566129e-10


def noise(v):
    """db_noise: the lane's own noise source (v = SynLane)."""
    v.rng, n = xorshift(v.rng)
    return n


METAL_INC = [hz * INV_SR for hz in METAL_HZ]    # db_metal_inc


def metal_sq(hm, mult):
    """db_metal_sq: advance the six metal phases (list of 6 floats) and
    return the mean of the squares at mult x their phase."""
    acc = 0.0
    for j in range(6):
        h = wrap(hm[j] + METAL_INC[j])
        hm[j] = h
        acc += sq(wrap(h * mult))
    return acc * 0.16666667


class State:
    """Attribute bag for a synth drum's state words (the C struct's fields)."""


class SynLane:
    """Mirror of DBSyn in drumbox.c: a lane playing a synth drum."""

    def __init__(self, seed):
        self.active = False
        self.rng = seed
        self.fade, self.fadeStep = 0.0, 0.0
        self.st = State()
