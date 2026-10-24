"""Synth open hi-hat: synth_hat with a long decay, on
synth_cymbal's code. A closed hat on its lane chokes it."""

ALGO = "synth_cymbal"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(lvl=0.8219, dec=0.99985, hp=0.7, metal=0.4, noise_lp=1.0,
              ping=0.0, dark=0.0, lp_max=1.0, swell=0.0, mult=1.0)
