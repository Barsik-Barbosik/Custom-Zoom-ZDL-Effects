"""Synth kick: synth_drum's code with
its full set (pitch drop, skin partial, drive, beater click)."""

ALGO = "synth_drum"
LEVEL = 0.30    # loudness of kick1
PARAMS = dict(
    f=58.0, sweep=0.9, sweep_dec=0.9975, dec=0.99975, lvl=0.54,
    click=0.30, click_lp=0.35, click_dec=0.993,
    punch=0.22, punch_ratio=2.3, punch_dec=0.9993, drive=0.3,
    noise_hp=0.0, phase0=0.0,
)
