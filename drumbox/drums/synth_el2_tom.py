"""High tom (Elec2 kit): synth_drum's code, a sine
gliding down to 130 Hz with a long-ish decay and a faint noise tick.
synth_el2_tom_lo.py is the low tom."""

# lvl: peak 20 ms RMS matched to the Synth kit drum on its lane
ALGO = "synth_drum"
LEVEL = 0.5
PARAMS = dict(f=130.0, sweep=0.35, sweep_dec=0.9993, dec=0.9996, lvl=0.47,
              click=0.05, click_lp=0.5, click_dec=0.995, punch=0.0, drive=0.0)
