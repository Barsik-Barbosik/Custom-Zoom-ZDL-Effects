"""Second EPiano for SongBox: Rhodes and FM-style (DX) candidates on the one
instrument engine (instruments/engine.py), rendered as WAVs to pick from.

    python -B ep2_proto.py      -> preview/ep2/ep2_NN_<name>.wav

Each WAV: the first 12 bars of proto.py's song, keys alone, then the same
bars with the precision bass, 1 s tail. 00 = the current EPiano for A/B.
Every candidate's LEVEL is set so its keys-alone RMS equals EPiano's (the
printed LEVEL goes into the instrument file).

Rhodes: a nearly pure sine body; the bark = the strike (menv, fast decay)
driving the soft clip with an even part; the tine = HIGH (a sine) read at
ip << sh for a few ms (b3 bp). FM (DX E.Piano style): a sine body, the
metallic tine = HIGH read at ip2 = 14 x ip (pc), fading fast with menv
(q2); the attack brightness = HIGH at ip << 1 (2nd harmonic, b3 bp), fading
slower. "cheap" candidates use EPiano's table (only a new record).
"""
from __future__ import annotations

import math
import sys
import types
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import proto  # noqa: E402
from sets import load  # noqa: E402
import engine  # noqa: E402  (sets put instruments/ on the path)
from engine import TWO32  # noqa: E402
from inst_lib import SR, coef, lp_coef, rise  # noqa: E402

BARS = 12
_DC = lp_coef(20.0)
_BLOCK = dict(la=1.0 - _DC, dc=_DC, post=1.0 - _DC)      # DC blocker (the clip's even part)
_SINE = [(1, 1.0, "sin")]


def _rhodes(**kw) -> dict:
    rec = dict(att=rise(3), dec=coef(4500), rel=coef(250),
               ma=coef(300), mk=0.0, m0=1.0,             # strike: menv 1 at the note, decays
               g0=0.45, g1=0.8, e=0.06,                  # bark: the strike drives the clip
               sh=3, bp0=1.0, bdec=coef(70), b3=0.35,    # tine: HIGH at 8 x, ~70 ms
               **_BLOCK)
    rec.update(kw)
    return rec


def _fm(**kw) -> dict:
    rec = dict(att=rise(2), dec=coef(3000), rel=coef(300),
               pc=14.0 * TWO32,                          # ip2 = 14 x ip: the tine
               ma=coef(90), mk=0.0, m0=1.0, q2=0.35,     # tine fades with menv, ~90 ms
               sh=1, bp0=1.0, bdec=coef(500), b3=0.5,    # 2nd harmonic, ~0.5 s
               g0=0.5, e=0.03, **_BLOCK)
    rec.update(kw)
    return rec


def _trem(hz: float, depth: float) -> dict:
    return dict(rot=int(hz / SR * TWO32), t0=1.0 - depth, ts=-depth)


# vibrato: pitch x (1 + pb tf / 2^32), tf 0..2^30, centred by pa
def _vib(hz: float, cents: float) -> dict:
    d = 2.0 ** (cents / 1200.0) - 1.0                  # +- d
    return dict(rot=int(hz / SR * TWO32), pa=TWO32 * (1.0 - d), pc=14.0 * TWO32 * (1.0 - d),
                pb=2.0 * d * TWO32 / 1073741824.0)


RH_LOW = [(1, 1.0, "sin"), (2, 0.05, "cos")]
FM_LOW = [(1, 1.0, "sin"), (2, 0.1, "cos")]

# (name, doc, LOW, HIGH, REC, table): table "epiano" = EPiano's table (cheap)
CANDIDATES = [
    ("rh_tine", "Rhodes: sine body, light bark, tine at 8 x for ~70 ms",
     RH_LOW, _SINE, _rhodes(), None),
    ("rh_bark", "Rhodes, hard playing: more bark (strike x 1.4, even part 0.12), less tine",
     RH_LOW, _SINE, _rhodes(g1=1.4, e=0.12, ma=coef(400), b3=0.25), None),
    ("rh_bell", "Rhodes, glassy / Dyno: tine at 16 x for ~120 ms, little bark, a touch more 2nd",
     [(1, 1.0, "sin"), (2, 0.1, "cos")], _SINE,
     _rhodes(g1=0.5, sh=4, b3=0.3, bdec=coef(120)), None),
    ("rh_suitcase", "Rhodes Suitcase: rh_tine + deep tremolo (4.5 Hz, 25 %)",
     RH_LOW, _SINE, _rhodes(**_trem(4.5, 0.25)), None),
    ("rh_cheap", "rh_tine's record on EPiano's table (record only, no new table)",
     None, None, _rhodes(), "epiano"),
    ("fm_ep1", "FM / DX E.Piano: sine body, tine at 14 x (~90 ms), 2nd harmonic attack (~0.5 s)",
     FM_LOW, _SINE, _fm(), None),
    ("fm_bright", "FM, 80s ballad: louder tine and attack",
     FM_LOW, _SINE, _fm(q2=0.6, b3=0.8, bdec=coef(350), ma=coef(120)), None),
    ("fm_soft", "FM, soft: quiet tine, gentle attack, longer decay, light tremolo",
     FM_LOW, _SINE, _fm(q2=0.15, b3=0.3, dec=coef(5000), **_trem(5.0, 0.08)), None),
    ("fm_chorus", "FM + slow vibrato (0.7 Hz, +- 4 cents): a chorused DX",
     FM_LOW, _SINE, _fm(**_vib(0.7, 4.0)), None),
    ("fm_cheap", "fm_ep1's record on EPiano's table (record only, no new table)",
     None, None, _fm(), "epiano"),
]

_SILENT = types.SimpleNamespace(render=lambda v: 0.0, LEVEL=0.0)


def _inst(i: int):
    if i < 0:
        return engine.Instrument(load("epiano"))
    name, _doc, low, high, rec, tab = CANDIDATES[i]
    mod = types.SimpleNamespace(__name__=name, LOW=low, HIGH=high, REC=rec)
    return engine.Instrument(mod, load(tab, "table") if tab else None)


def keys_track(i: int) -> list:
    ik = types.SimpleNamespace(render=_inst(i).render, LEVEL=1.0)
    return proto.render_with(ik, _SILENT, proto.SONG[:BARS])


def bass_track() -> list:
    mod = load("pbass")
    ib = types.SimpleNamespace(render=engine.Instrument(mod).render, LEVEL=mod.LEVEL)
    return proto.render_with(_SILENT, ib, proto.SONG[:BARS], keys_on=False)


def _rms(x: list) -> float:
    return math.sqrt(sum(s * s for s in x) / len(x))


def main() -> None:
    out = HERE / "preview" / "ep2"
    out.mkdir(parents=True, exist_ok=True)
    n = BARS * 16 * proto.STEP
    idx = [-1] + list(range(len(CANDIDATES)))
    with ProcessPoolExecutor() as ex:
        bf = ex.submit(bass_track)
        keys = list(ex.map(keys_track, idx))
        bass = bf.result()
    ref_level = load("epiano").LEVEL
    ref = _rms(keys[0]) * ref_level
    for j, (i, k) in enumerate(zip(idx, keys)):
        lv = ref_level if i < 0 else ref / _rms(k)
        name, doc = ("epiano", "the current EPiano (reference)") if i < 0 else CANDIDATES[i][:2]
        kk = [s * lv for s in k]
        mix = [max(-0.99, min(0.99, x + y)) for x, y in zip(kk, bass)]
        print(f"{j:2}: {name}: LEVEL {lv:.4f}  {doc}")
        proto.write_wav(out / f"ep2_{j:02}_{name}.wav", [max(-0.99, min(0.99, s)) for s in kk[:n]] + mix)


if __name__ == "__main__":
    main()
