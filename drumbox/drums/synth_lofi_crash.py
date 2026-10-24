"""Lo-fi crash (LoFi kit): dark noise wash over two driven
sines (1.5 / 2.3 kHz), crushed."""

ALGO = "synth_drum"
LEVEL = 0.11    # as loud as synth_el2_crash (lowered by ear)
PARAMS = dict(f=1500.0, sweep=0.0, dec=0.9998, lvl=0.1, punch=1.0, punch_ratio=1.53,
              punch_dec=0.9998, drive=3.0, click=0.8, click_lp=0.55, click_dec=0.9999,
              noise_hp=0.35,
              crush_rate=0.27, crush_q=32.0, crush_step=1 / 32.0)
