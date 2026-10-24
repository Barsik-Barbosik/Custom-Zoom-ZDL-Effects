"""8-bit kick (Elec1 kit): synth_drum's code driven into a
near-square (db_soft_clip is +-1 above 1.5, drive 6) with a big, fast pitch
drop (200 -> 50 Hz), no skin partial, no click."""

ALGO = "synth_drum"
LEVEL = 0.30    # as synth_kick
PARAMS = dict(f=50.0, sweep=3.0, sweep_dec=0.9990, dec=0.9996, lvl=0.35,
              click=0.0, punch=0.0, drive=6.0)
