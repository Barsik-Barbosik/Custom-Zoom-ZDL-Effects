"""Low tom (Elec2 kit): synth_el2_tom at 90 Hz."""

# lvl: peak 20 ms RMS matched to the Synth kit drum on its lane
ALGO = "synth_drum"
LEVEL = 0.5
PARAMS = dict(f=90.0, sweep=0.35, sweep_dec=0.9993, dec=0.9996, lvl=0.48,
              click=0.05, click_lp=0.5, click_dec=0.995, punch=0.0, drive=0.0)
