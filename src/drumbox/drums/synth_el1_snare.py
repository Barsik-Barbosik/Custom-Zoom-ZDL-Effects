"""8-bit snare (Elec1 kit): synth_drum's code, a short
near-square body (drive 6) dropping from 360 to 180 Hz plus raw white noise
(click_lp 1 = no low-pass, noise_hp 0.3 thins it)."""

ALGO = "synth_drum"
LEVEL = 0.5
PARAMS = dict(f=180.0, sweep=1.0, sweep_dec=0.998, dec=0.9985, lvl=0.2,
              punch=0.0, drive=6.0,
              click=0.5, click_lp=1.0, click_dec=0.9993, noise_hp=0.3)
