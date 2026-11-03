"""Synth snare: two sines (60 / 40 %)
plus noise through a one-pole high-pass and a one-pole low-pass, each part
with its own decay. Runs synth_drum's code: body = f1 without sweep, punch = the f2 sine at 0.4 / 0.6
of it, click = the noise (noise_hp = its high-pass, click_lp its
low-pass); same sound up to float rounding."""

ALGO = "synth_drum"
LEVEL = 0.5     # loudness of the kit 1 drum on its lane (the 0.5 mix trim)
_F1, _F2, _TONE_LVL, _TONE_DEC = 190.0, 330.0, 0.36, 0.9992
PARAMS = dict(
    f=_F1, sweep=0.0, dec=_TONE_DEC, lvl=0.6 * _TONE_LVL,
    punch=0.4 / 0.6, punch_ratio=_F2 / _F1, punch_dec=_TONE_DEC, drive=0.0,
    click=0.44, click_lp=0.60, click_dec=0.99955, noise_hp=0.30, phase0=0.25,
)
