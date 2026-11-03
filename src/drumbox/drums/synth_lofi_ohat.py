"""Lo-fi open hat (LoFi kit): synth_lofi_hat, long."""

ALGO = "synth_drum"
LEVEL = 0.34    # peak 20 ms RMS = synth_ohat
PARAMS = dict(f=3000.0, sweep=0.0, dec=0.9997, lvl=0.05, punch=1.0, punch_ratio=1.48,
              punch_dec=0.9997, drive=2.0, click=1.0, click_lp=0.45, click_dec=0.9997,
              noise_hp=0.5,
              crush_rate=0.27, crush_q=32.0, crush_step=1 / 32.0)
