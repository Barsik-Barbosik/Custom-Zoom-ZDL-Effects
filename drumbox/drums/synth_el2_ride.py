"""Ride (Elec2 kit, on synth_drum's code): mostly
tone, two sines at 2.9 / 4.0 kHz driven together (a bell-like ping that
rings long), with a little high-passed noise for the wash."""

# LEVEL: lowered by ear from the Synth-kit match (ride 0.111, crash 0.175)
ALGO = "synth_drum"
LEVEL = 0.02
PARAMS = dict(f=2900.0, sweep=0.0, dec=0.99992, lvl=0.3,
              punch=1.0, punch_ratio=1.37, punch_dec=0.9999, drive=1.5,
              click=0.25, click_lp=0.8, click_dec=0.9998, noise_hp=0.5)
