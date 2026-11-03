"""Kick (Elec2 kit): synth_drum's code, a long boomy
sine at 50 Hz with a small, quick pitch drop, a faint low-passed click, no
skin partial; decays over ~1.5 s."""

# lvl: peak 20 ms RMS matched to the Synth kit drum on its lane
ALGO = "synth_drum"
LEVEL = 0.30    # as synth_kick
PARAMS = dict(f=50.0, sweep=0.6, sweep_dec=0.998, dec=0.99990, lvl=0.55,
              click=0.08, click_lp=0.3, click_dec=0.99, punch=0.0, drive=0.1)
