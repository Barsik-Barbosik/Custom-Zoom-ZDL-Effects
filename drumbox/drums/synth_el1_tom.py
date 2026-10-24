"""8-bit high tom (Elec1 kit): synth_drum's code, a
near-square (drive 6) dropping from 275 to 110 Hz; punch and click off.
synth_el1_tom_lo.py is the low tom."""

ALGO = "synth_drum"
LEVEL = 0.5
PARAMS = dict(f=110.0, sweep=1.5, sweep_dec=0.9985, dec=0.9994, lvl=0.3,
              click=0.0, punch=0.0, drive=6.0)
