"""Synth hi-hat, closed. Runs synth_cymbal's code (the ride, hats and
crash share it): noise + the ring-modulated metal bank at x1, two high-passes,
one decay; the ride's closing low-pass, ping and swell off.
synth_ohat.py is the open hat."""

ALGO = "synth_cymbal"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(lvl=0.8314, dec=0.9984, hp=0.7, metal=0.4, noise_lp=1.0,
              ping=0.0, dark=0.0, lp_max=1.0, swell=0.0, mult=1.0)
