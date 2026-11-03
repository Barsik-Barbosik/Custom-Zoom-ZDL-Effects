"""Open hat (Elec2 kit, on synth_drum's code):
synth_el2_hat with ~150 ms decays. A closed hat on its lane chokes it."""

# LEVEL: peak 20 ms RMS matched to the Synth kit drum on its lane
ALGO = "synth_drum"
LEVEL = 0.190
PARAMS = dict(f=3400.0, sweep=0.0, dec=0.99985, lvl=0.1,
              punch=1.0, punch_ratio=1.4815, punch_dec=0.99985, drive=3.0,
              click=1.0, click_lp=1.0, click_dec=0.99985, noise_hp=0.7)
