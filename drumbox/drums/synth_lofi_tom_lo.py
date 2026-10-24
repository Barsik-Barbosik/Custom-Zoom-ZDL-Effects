"""Lo-fi low tom (LoFi kit): synth_lofi_tom at 95 Hz."""

ALGO = "synth_drum"
LEVEL = 0.5     # peak 20 ms RMS = synth_tom
PARAMS = dict(f=95.0, sweep=0.3, sweep_dec=0.9993, dec=0.9994, lvl=0.45, punch=0.0,
              drive=0.5, click=0.06, click_lp=0.2, click_dec=0.995,
              crush_rate=0.27, crush_q=24.0, crush_step=1 / 24.0)
