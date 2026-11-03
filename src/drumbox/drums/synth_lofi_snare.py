"""Lo-fi snare (LoFi kit): two-tone body with dark, low-passed
noise, a little drive, crushed."""

ALGO = "synth_drum"
LEVEL = 0.63    # peak 20 ms RMS = synth_snare
PARAMS = dict(f=180.0, sweep=0.1, sweep_dec=0.998, dec=0.9985, lvl=0.25,
              punch=0.5, punch_ratio=1.6, punch_dec=0.998, drive=0.6,
              click=0.4, click_lp=0.25, click_dec=0.9990, noise_hp=0.15, phase0=0.25,
              crush_rate=0.27, crush_q=20.0, crush_step=1 / 20.0)
