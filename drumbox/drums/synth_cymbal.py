"""Synth cymbal code: the metal bank at x12 ring-modulated (sign
products of square pairs: dense, no pitch) mixed with low-passed noise, two
one-pole high-passes, a low-pass that closes as the hit decays (dark,
lp_max), an optional swell (rise rate swell_rate, 0.0008 = ~30 ms)
and a short metal stick ping.
Plays the ride, hats and crash (synth_ride.py, synth_hat.py etc.):
mult = metal
bank pitch (ride x12), dark 0 + lp_max 1 = no closing low-pass, noise_lp
1 = plain noise. C: synth_cymbal.c (keep in sync). PARAMS = the ride's."""

from synth_lib import METAL_INC, frac, lerp, noise, sq, wrap

LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(
    lvl=0.165, dec=0.99993, hp=0.14, metal=0.8, noise_lp=0.5,
    ping=0.7, dark=0.7, lp_max=1.0, swell=0.0, mult=12.0, swell_rate=0.0008,
)
STATE = dict(amp=1, lp1=1, lp2=1, nlp=1, lp=1, att=1, ping=1, p1=1, hm=6, m=6)


def hit(v, p, vel):
    y = v.st
    y.amp = vel
    y.ping = vel
    y.att = 1.0 - p["swell"]


def render(v, p):
    y = v.st
    if y.amp <= 0.00005:
        y.amp = 0.0
        y.ping = 0.0
        v.active = False
        return 0.0
    n = noise(v)
    hm, m = y.hm, y.m
    for j in range(6):
        h = wrap(hm[j] + METAL_INC[j])
        hm[j] = h
        m[j] = sq(frac(h * p["mult"]))
    c = p["hp"]
    rmC = (m[0] * m[3] + m[1] * m[4] + m[2] * m[5] + m[0] * m[5] * m[2]) * 0.25
    y.nlp += (n - y.nlp) * p["noise_lp"]
    src = lerp(y.nlp * 2.0, rmC, p["metal"])
    y.lp1 += (src - y.lp1) * c
    hp = src - y.lp1
    y.lp2 += (hp - y.lp2) * c
    hp = hp - y.lp2
    k = 1.0 - p["dark"] * (1.0 - y.amp)
    k = 0.08 + (p["lp_max"] - 0.08) * k * k
    y.lp += (hp - y.lp) * k
    if y.att < 1.0:
        y.att += (1.0 - y.att) * p["swell_rate"]
    o = y.lp * y.amp * y.att
    if y.ping > 0.00005:
        y.p1 += (rmC - y.p1) * 0.85
        o += (rmC - y.p1) * y.ping * p["ping"]
        y.ping *= 0.9985
    y.amp *= p["dec"]
    return o * p["lvl"]
