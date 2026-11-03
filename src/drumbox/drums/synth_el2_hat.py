"""Closed hat (Elec2 kit: on synth_drum's code):
high-passed white noise (the click path: noise_hp high-pass, no low-pass)
plus two sines at 3.4 / 5.0 kHz driven together (drive 3; their
intermodulation = the metallic ring), no sweep, ~15 ms decay.
synth_el2_ohat.py is the open hat."""

# LEVEL: peak 20 ms RMS matched to the Synth kit drum on its lane
ALGO = "synth_drum"
LEVEL = 0.184
PARAMS = dict(f=3400.0, sweep=0.0, dec=0.997, lvl=0.1,
              punch=1.0, punch_ratio=1.4815, punch_dec=0.997, drive=3.0,
              click=1.0, click_lp=1.0, click_dec=0.9985, noise_hp=0.7)
