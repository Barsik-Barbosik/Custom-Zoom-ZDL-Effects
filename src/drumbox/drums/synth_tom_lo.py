"""Synth low tom: synth_tom tuned lower, at 75 Hz (about the ratio of
the sampled low tom to the high tom).
synth_drum's code with punch, drive and click off, like synth_tom."""

ALGO = "synth_drum"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
PARAMS = dict(f=75.0, sweep=0.25, sweep_dec=0.9990, dec=0.99975, lvl=0.45,
              click=0.0, punch=0.0, drive=0.0)
