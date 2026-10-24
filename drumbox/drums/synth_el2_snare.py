"""Snare (Elec2 kit): synth_drum's code, two short
tones (185 / 335 Hz, punch = the upper one) and high-passed noise snap."""

# lvl: peak 20 ms RMS matched to the Synth kit drum on its lane
ALGO = "synth_drum"
LEVEL = 0.5
PARAMS = dict(f=185.0, sweep=0.15, sweep_dec=0.998, dec=0.9990, lvl=0.26,
              punch=0.6, punch_ratio=1.8, punch_dec=0.9986, drive=0.0,
              click=0.35, click_lp=0.7, click_dec=0.9993, noise_hp=0.35, phase0=0.25)
