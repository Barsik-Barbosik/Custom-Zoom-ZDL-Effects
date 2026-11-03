"""Synth tom, high: a sine with a mild
pitch sweep and one decay. Runs synth_drum's code with punch,
drive and click off. synth_tom_lo.py is the low tom."""

ALGO = "synth_drum"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(f=110.0, sweep=0.25, sweep_dec=0.9990, dec=0.99975, lvl=0.45,
              click=0.0, punch=0.0, drive=0.0)
