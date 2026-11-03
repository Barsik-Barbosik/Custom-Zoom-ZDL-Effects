"""Lo-fi closed hat (LoFi kit): dull band-passed noise plus two
quiet driven high sines (as synth_el2_hat, darker), crushed: the hold
folds the highs back down (grit)."""

ALGO = "synth_drum"
LEVEL = 0.43    # peak 20 ms RMS = synth_hat
PARAMS = dict(f=3000.0, sweep=0.0, dec=0.996, lvl=0.05, punch=1.0, punch_ratio=1.48,
              punch_dec=0.996, drive=2.0, click=1.0, click_lp=0.45, click_dec=0.997,
              noise_hp=0.5,
              crush_rate=0.27, crush_q=32.0, crush_step=1 / 32.0)
