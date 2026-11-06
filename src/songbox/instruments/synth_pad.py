"""Synth pad (keys): two detuned saws, slow attack, two one-pole low-passes.
C: synth_pad.c (keep in sync)."""

from inst_lib import coef, lp_coef, wrap

ROLE = "keys"
LEVEL = 0.07
CONST = dict(att=1.0 - coef(250), rel=coef(400), lp=lp_coef(1800.0))


def render(v) -> float:
    c = CONST
    v.p = wrap(v.p + v.f * 1.004); v.p2 = wrap(v.p2 + v.f * 0.996)
    if v.gate > 0:
        v.env += (1.0 - v.env) * c["att"]
    else:
        v.env *= c["rel"]
    saw = v.p + v.p2 - 1.0
    v.lp += (saw - v.lp) * c["lp"]
    v.lp2 += (v.lp - v.lp2) * c["lp"]
    return v.lp2 * v.env
