"""Lo-fi kick (LoFi kit): a short, dusty 55 Hz thump, a dull
low-passed beater, mild drive, crushed (~12 kHz, coarse steps)."""

ALGO = "synth_drum"
LEVEL = 0.3
PARAMS = dict(f=55.0, sweep=0.8, sweep_dec=0.997, dec=0.9994, lvl=0.5,
              punch=0.15, punch_ratio=2.0, punch_dec=0.999, drive=0.8,
              click=0.15, click_lp=0.12, click_dec=0.995, noise_hp=0.0,
              crush_rate=0.27, crush_q=24.0, crush_step=1 / 24.0)
