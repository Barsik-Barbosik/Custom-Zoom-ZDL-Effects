"""Finger bass: sine plus a low-passed saw, plucked (the low-pass closes).
C: finger_bass.c (keep in sync)."""

from inst_lib import coef, lp_coef, psin, wrap

ROLE = "bass"
LEVEL = 0.30
CONST = dict(bdec=coef(700), brel=coef(60), blp=lp_coef(900.0), blpdec=coef(300))


def render(v) -> float:
    c = CONST
    v.p = wrap(v.p + v.f)
    if v.att < 1.0:
        v.att = 1.0; v.env = 1.0; v.menv = 1.0
    v.env *= c["bdec"] if v.gate > 0 else c["brel"]
    v.menv *= c["blpdec"]
    saw = v.p * 2.0 - 1.0
    v.lp += (saw - v.lp) * (c["blp"] * (0.25 + 0.75 * v.menv))
    return (0.8 * psin(v.p) + 0.6 * v.lp) * v.env
