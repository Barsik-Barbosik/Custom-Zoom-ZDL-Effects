"""Crash (Elec2 kit, on synth_drum's code): mostly
high-passed noise with a long decay, under two driven sines at 1.7 /
2.6 kHz for the metal."""

# LEVEL: lowered by ear from the Synth-kit match (ride 0.111, crash 0.175)
ALGO = "synth_drum"
LEVEL = 0.06
PARAMS = dict(f=1700.0, sweep=0.0, dec=0.9999, lvl=0.15,
              punch=1.0, punch_ratio=1.53, punch_dec=0.9999, drive=3.0,
              click=1.0, click_lp=0.9, click_dec=0.99993, noise_hp=0.45)
