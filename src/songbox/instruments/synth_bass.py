"""Synth bass: saw through a resonant state-variable low-pass whose
frequency follows a decaying envelope. C: synth_bass.c (keep in sync)."""

from inst_lib import coef, wrap

ROLE = "bass"
LEVEL = 0.28 * 0.9
CONST = dict(bdec=coef(1200), brel=coef(80), fdec=coef(220))


def render(v) -> float:
    c = CONST
    v.p = wrap(v.p + v.f)
    if v.att < 1.0:
        v.att = 1.0; v.env = 1.0; v.menv = 1.0
    v.env *= c["bdec"] if v.gate > 0 else c["brel"]
    v.menv *= c["fdec"]
    f = 0.02 + 0.18 * v.menv                       # SVF frequency coefficient
    saw = v.p * 2.0 - 1.0
    hp = saw - v.lp - 0.5 * v.bp                   # q = 0.5: some resonance
    v.bp += f * hp
    v.lp += f * v.bp
    return v.lp * v.env
