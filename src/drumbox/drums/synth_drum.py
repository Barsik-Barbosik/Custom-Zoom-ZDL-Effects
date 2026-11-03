"""Synth drum code: sine body with a
small, fast pitch drop, a "skin" partial at punch_ratio x pitch, mild drive
and a low-passed decaying beater click. Decays are per-sample multipliers.
Plays the kick (synth_kick.py), the toms (punch, drive, click off) and the
snare (synth_snare.py: no sweep, punch = its second sine, click = its
noise, high-passed by noise_hp, phases start at phase0); plays until body
and click have decayed.
Crush (LoFi kit): crush_rate > 0 holds the output, resampled
crush_rate x 44.1 kHz, truncated to steps of crush_step (crush_q =
1 / crush_step); 0 = off.
C: synth_drum.c (keep in sync). PARAMS = the kick's."""

from synth_lib import INV_SR, db_sin, noise, soft_clip, wrap

LEVEL = 0.30    # loudness of kick1
PARAMS = dict(
    f=58.0, sweep=0.9, sweep_dec=0.9975, dec=0.99975, lvl=0.54,
    click=0.30, click_lp=0.35, click_dec=0.993,
    punch=0.22, punch_ratio=2.3, punch_dec=0.9993, drive=0.3,
    noise_hp=0.0, phase0=0.0, crush_rate=0.0, crush_q=1.0, crush_step=1.0,
)
STATE = dict(phase=1, amp=1, pitch=1, click=1, clickLp=1, punchPh=1, punch=1, hpLp=1,
             hold=1, held=1)


def hit(v, p, vel):
    k = v.st
    k.amp = vel
    k.pitch = 1.0
    k.click = vel
    k.punch = vel
    k.phase = p["phase0"]    # kick: 0, a zero crossing: no step click
    k.punchPh = p["phase0"]
    k.hpLp = 0.0             # the lane's last drum may have left it set
    k.hold = 1.0             # crush: take the first sample
    k.held = 0.0


def render(v, p):
    k = v.st
    if k.amp <= 0.00005 and k.click <= 0.00005:
        k.amp = 0.0
        v.active = False
        return 0.0
    n = noise(v)
    f = p["f"] * (1.0 + p["sweep"] * k.pitch)
    k.phase = wrap(k.phase + f * INV_SR)
    ks = db_sin(k.phase) * k.amp
    if p["punch"] > 0.0:
        k.punchPh = wrap(k.punchPh + f * p["punch_ratio"] * INV_SR)
        ks += db_sin(k.punchPh) * k.punch * p["punch"]
        k.punch *= p["punch_dec"]
    if p["drive"] > 0.0:
        ks = soft_clip(ks * (1.0 + p["drive"]))
    out = ks * p["lvl"]
    k.hpLp += (n - k.hpLp) * p["noise_hp"]     # 0: hpLp stays 0
    n -= k.hpLp
    k.clickLp += (n - k.clickLp) * p["click_lp"]
    out += k.clickLp * k.click * p["click"]
    k.click *= p["click_dec"]
    k.pitch *= p["sweep_dec"]
    k.amp *= p["dec"]
    if p["crush_rate"] > 0.0:
        if k.hold >= 1.0:
            k.hold -= 1.0
            k.held = float(int(out * p["crush_q"])) * p["crush_step"]
        k.hold += p["crush_rate"]
        out = k.held
    return out
