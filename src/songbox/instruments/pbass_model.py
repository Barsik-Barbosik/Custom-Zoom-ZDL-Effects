"""Precision bass model for SongBox: harmonic amplitudes from a plucked
string (plucked at `pluck`, sensed by a pickup at `pickup`, fractions of the
string from the bridge; velocity pickup ~ 1 / h^k; tone pot low-pass at
harmonic hc) mapped onto the one instrument engine (engine.py):
  LOW  = body, harmonics 1..3 (always on, g0)
  HIGH = harmonics 4..NH read at ip2 = ip, gain r2 (steady) + q2 menv
         (slow brightness decay, ma) + b2 bp (fast pluck brightness, bdec):
         a low-pass closing after the attack
  click = HIGH read at ip << sh, gain b3 bp (finger / pick noise)
  dec = string decay, rel = mute at the gate end, g0 into the soft clip
  (drive, e = even part), DC blocker (la, dc).
VARIANTS = the prototypes (pbass_proto.py renders them); an instrument file
takes one: ROLE, LEVEL, LOW, HIGH, REC = pick("<name>").
"""
from __future__ import annotations

import math
import types

import engine
from inst_lib import coef, lp_coef, rise

NH = 20                         # harmonics in the table
PICKUP = 0.19                   # P pickup: ~6.5" from the bridge on a 34" scale
_DC = lp_coef(8.0)              # DC blocker below E1 (41 Hz)


def terms(pluck: float, k: float, hc: float, pickup: float = PICKUP) -> tuple:
    """(LOW, HIGH, ratio): the string model's harmonics 1..3 and 4..NH as
    engine terms; ratio = HIGH's peak / LOW's (the engine scales each half
    to its own peak, so r2 / q2 / b2 = 1 x ratio keeps the model's balance)."""
    amps = []
    for h in range(1, NH + 1):
        a = math.sin(math.pi * h * pluck) * math.sin(math.pi * h * pickup)
        a /= h ** k * math.sqrt(1.0 + (h / hc) ** 2)
        if abs(a) > 1e-9:                   # (nodes of the pluck point: no term)
            amps.append((h, a, "sin"))
    low = [t for t in amps if t[0] <= 3]
    high = [t for t in amps if t[0] > 3]
    return low, high, engine._peak(high) / engine._peak(low)


def _y(x: float, e: float) -> float:
    """The engine's soft clip of x (1.5 x for small x)."""
    c = abs(x + 1.0) - abs(x - 1.0)
    return 0.75 * c + c * c * (e - 0.0625 * c)


def variant(name: str, doc: str, pluck: float, k: float, hc: float, *, g0=0.35, g1=0.0,
            r2=0.3, q2=0.5, ma=600, b2=0.6, bdec=60, b3=0.0, sh=3, att=3, dec=4000,
            rel=80, e=0.0, level=0.35):
    """An instrument record; brightness gains in model units, times in ms."""
    low, high, ratio = terms(pluck, k, hc)
    pk = _y(g0 * (1.0 + g1 + ratio * (r2 + q2 + b2)), e)    # attack peak, about
    rec = dict(att=rise(att), dec=coef(dec), rel=coef(rel),
               g0=g0, g1=g0 * g1, ma=coef(ma), mk=0.0, m0=1.0,
               r2=r2 * ratio, q2=q2 * ratio, b2=b2 * ratio, b3=b3 * ratio,
               bp0=1.0, bdec=coef(bdec), sh=sh,
               e=e, la=1.0 - _DC, dc=_DC, post=0.6 / pk * (1.0 - _DC))
    return types.SimpleNamespace(__name__=name, NAME=name, DOC=doc, ROLE="bass",
                                 LEVEL=level, LOW=low, HIGH=high, REC=rec)


VARIANTS = [
    variant("fing_open", "fingers over the pickup, roundwounds, tone open",
            0.20, 1.0, 12, r2=0.35, q2=0.65, ma=700, b2=0.6, bdec=60, b3=0.08),
    variant("fing_warm", "fingers, tone rolled off",
            0.22, 1.1, 5, r2=0.2, q2=0.5, ma=400, b2=0.3, bdec=50, b3=0.03),
    variant("pick_bright", "pick near the bridge, strong highs and click",
            0.12, 0.9, 20, g0=0.4, r2=0.5, q2=0.8, ma=500, b2=1.2, bdec=40, b3=0.35,
            sh=4, att=1),
    variant("flats_motown", "flatwounds, dark, short sustain, thump",
            0.30, 1.6, 4, g1=0.4, r2=0.1, q2=0.4, ma=200, b2=0.5, bdec=25, b3=0.05,
            dec=1200, rel=50),
    variant("pick_muted", "palm-muted pick, short notes",
            0.12, 1.1, 10, r2=0.2, q2=0.5, ma=150, b2=1.0, bdec=30, b3=0.3, sh=4,
            att=1, dec=350, rel=40),
    variant("thumb", "thumb near the neck, soft attack, very dark",
            0.35, 1.4, 4, r2=0.1, q2=0.3, ma=300, b2=0.2, bdec=40, att=8, dec=2500),
    variant("fing_grit", "fingers into a light drive",
            0.20, 1.0, 9, g0=0.9, e=0.05, r2=0.35, q2=0.6, ma=600, b2=0.6, bdec=60,
            b3=0.08),
    variant("bridge_nasal", "fingers near the bridge, hollow mids",
            0.08, 0.9, 14, r2=0.6, q2=0.5, ma=800, b2=0.6, bdec=60, b3=0.1),
    variant("round_sustain", "modern roundwounds, long sustain, compressed",
            0.20, 0.9, 16, g0=0.6, e=0.02, r2=0.55, q2=0.5, ma=1500, b2=0.5, bdec=80,
            b3=0.1, dec=9000, rel=100),
    variant("pick_drive", "pick into drive, rock",
            0.12, 0.9, 16, g0=1.3, e=0.07, r2=0.6, q2=0.7, ma=600, b2=1.0, bdec=40,
            b3=0.3, sh=4, att=1, dec=5000),
]



def pick(name: str) -> tuple:
    """(ROLE, LEVEL, LOW, HIGH, REC) of variant name, for an instrument file."""
    v = next(v for v in VARIANTS if v.NAME == name)
    return v.ROLE, v.LEVEL, v.LOW, v.HIGH, dict(v.REC)
