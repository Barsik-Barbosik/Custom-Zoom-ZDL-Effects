"""Lo-fi high tom (LoFi kit): 140 Hz sine with a small drop, a
dull click, mild drive, crushed."""

ALGO = "synth_drum"
LEVEL = 0.5     # peak 20 ms RMS = synth_tom
PARAMS = dict(f=140.0, sweep=0.3, sweep_dec=0.9993, dec=0.9993, lvl=0.45, punch=0.0,
              drive=0.5, click=0.06, click_lp=0.2, click_dec=0.995,
              crush_rate=0.27, crush_q=24.0, crush_step=1 / 24.0)
