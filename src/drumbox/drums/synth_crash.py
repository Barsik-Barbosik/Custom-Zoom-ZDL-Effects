"""Synth crash : noise and the metal bank
an octave up, two high-passes, a long decay. Runs synth_cymbal's code
with the ride's closing low-pass, ping and swell off."""

ALGO = "synth_cymbal"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(lvl=0.9175, dec=0.99994, hp=0.6, metal=0.55, noise_lp=1.0,
              ping=0.0, dark=0.0, lp_max=1.0, swell=0.0, mult=2.0)
