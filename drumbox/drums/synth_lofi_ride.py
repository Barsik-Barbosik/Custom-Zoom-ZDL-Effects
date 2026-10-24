"""Lo-fi ride lane (LoFi kit): a half-open hat, between the closed and
open hat in length (~150 ms), brighter noise and its own tones (3.8 /
5.0 kHz), crushed harder (~8.8 kHz hold, coarser steps) for more grit."""

ALGO = "synth_drum"
LEVEL = 0.235   # peak 20 ms RMS = the closed hats (synth_hat, synth_lofi_hat)
PARAMS = dict(f=3800.0, sweep=0.0, dec=0.9990, lvl=0.06, punch=1.0, punch_ratio=1.31,
              punch_dec=0.9990, drive=2.5, click=1.0, click_lp=0.6, click_dec=0.9992,
              noise_hp=0.6,
              crush_rate=0.2, crush_q=24.0, crush_step=1 / 24.0)
