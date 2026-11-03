"""Synth ride: synth_cymbal's code
(ring-modulated metal at x12, darkening low-pass, stick ping)."""

ALGO = "synth_cymbal"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(
    lvl=0.165, dec=0.99993, hp=0.14, metal=0.8, noise_lp=0.5,
    ping=0.7, dark=0.7, lp_max=1.0, swell=0.0, mult=12.0,
)
