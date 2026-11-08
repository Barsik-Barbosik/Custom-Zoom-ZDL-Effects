"""Atom library for SongBox sound exploration: small spectral pieces (atoms)
and behaviours (record templates) that combine them into candidate
instruments. Every candidate is plain engine data (LOW / HIGH terms + REC,
exactly what an instruments/<id>.py file holds), so it plays on the one
engine (engine.py / engine.c) unchanged: no code, no CPU change.

Atoms (ATOMS) are raw term lists (harmonic, amplitude, "sin" | "cos"), not
normalized: candidates use own-table semantics (engine.py Instrument with
tab None): g0 per unit of the LOW terms' sum, r2 / q2 / b2 / b3 per unit of
the HIGH terms' sum relative to that. make() sets g0 / post from the peak.
Near-zero terms are dropped: on the pedal each term is 4 B.

Engine tricks the candidates use (see engine.py's docstring):
  split   LOW = an atom's low harmonics, HIGH = the rest, phase-locked
          (m2 = 1, pc = pa): HIGH's gain r2 + q2 menv + b2 bp moves the
          brightness like a filter envelope (r2 = 1 = the whole atom)
  free    HIGH on its own phase (r0 = 1, m2 = 0) at pc: detuned copy
          (chorus, ensemble, musette) or an inharmonic partial (bells,
          mallets: pc not a whole multiple of pa)
  pa      the note's pitch ratio: 2 / 4 = one / two octaves up, 0.5 down
  b3, sh  HIGH read 2^sh x the note, gain b3 bp: a short tick / chiff
  rot     the LFO: pb = vibrato (triangle), ts = tremolo (sine)
  e       even harmonics in the soft clip (+ DC blocker), g0 = drive

BEHAVIOURS: name -> f(atom terms) -> (LOW, HIGH, REC), for the atom x
behaviour grid. NAMED: instrument emulations, name -> (role, LOW, HIGH,
REC). atom_grid.py renders both (preview/atoms/).
"""

from __future__ import annotations

import math

from engine import TWO32, _peak
from inst_lib import SR, coef, lp_coef, rise

PRE = 1.0 / 64.0        # clean: a read's peak into the clip (linear part)


# ------------------------------------------------------------------ helpers

def series(hs, amp, kind: str = "sin") -> list:
    """Terms (h, amp(h), kind) for h in hs, dropping |amp| < 0.004."""
    out = []
    for h in hs:
        a = amp(h)
        if abs(a) >= 0.004:
            out.append((h, a, kind))
    return out


def split(atom: list, k: int) -> tuple:
    """(harmonics <= k, harmonics > k) of an atom."""
    return [t for t in atom if t[0] <= k], [t for t in atom if t[0] > k]


def upto(atom: list, k: int) -> list:
    return [t for t in atom if t[0] <= k]


def R(x: float) -> float:
    """A pitch ratio as a phase increment factor (pa / pc)."""
    return x * TWO32


FREE = dict(r0=1, m2=0)             # HIGH on its own phase


def vib(rate: float, depth: float, ra: float = 1.0, rc: float | None = None) -> dict:
    """Vibrato at rate Hz, depth = peak-to-peak pitch fraction, around
    ratios ra (LOW) / rc (HIGH; None: = ra, phase-locked reads stay locked)."""
    rc = ra if rc is None else rc
    return dict(rot=int(rate / SR * TWO32), pb=4.0 * depth * ra,
                pa=R(ra - ra * depth / 2), pc=R(rc - ra * depth / 2))


def trem(rate: float, depth: float) -> dict:
    """Tremolo at rate Hz, depth = gain dip (0.3: 1.0 .. 0.7)."""
    return dict(rot=int(rate / SR * TWO32), t0=1.0 - depth / 2, ts=depth / 2)


def make(low, high=(), pre: float = PRE, reads: float = 1.0, peak: float = 0.0,
         acc: float = 0.0, dcblock: bool = False, **rec) -> tuple:
    """(LOW, HIGH, REC): g0 = pre / peak (peak: of low unless given; a split
    atom: the whole atom's), g1 = acc g0 (strike accent), post undoes pre
    (reads: how many reads add up), dcblock: 20 Hz DC blocker (with e)."""
    p = peak or _peak(list(low))
    rec.setdefault("g0", pre / p)
    if acc:
        rec["g1"] = acc * rec["g0"]
    rec.setdefault("post", 1.0 / (1.5 * pre * reads))
    if dcblock:
        d = lp_coef(20.0)
        rec.update(la=1.0 - d, dc=d)
    return list(low), list(high), rec


# ------------------------------------------------------------------ atoms

def _formant(n: int) -> float:
    """"ah": 1/n plus two bumps near harmonics 3-4 and 6 (~700 / 1200 Hz at
    the keys' ~200 Hz)."""
    return (1.0 / n + 0.6 * math.exp(-((n - 3.5) / 1.0) ** 2)
            + 0.35 * math.exp(-((n - 6.0) / 1.2) ** 2))


ATOMS = {
    "sine": [(1, 1.0, "sin")],
    "tri": series(range(1, 12, 2), lambda n: (-1.0 if n % 4 == 3 else 1.0) / (n * n)),
    "square": series(range(1, 16, 2), lambda n: 1.0 / n),
    "saw": series(range(1, 17), lambda n: 1.0 / n),
    "pulse25": series(range(1, 17), lambda n: math.sin(math.pi * n / 4) / n),     # reedy
    "pulse12": series(range(1, 17), lambda n: math.sin(math.pi * n / 8) / n),     # thin, nasal
    "hollow": [(1, 1.0, "sin"), (3, 0.55, "sin"), (5, 0.3, "sin"), (7, 0.15, "sin"),
               (2, 0.08, "cos"), (4, 0.05, "cos")],                             # clarinet-like
    "flute": [(1, 1.0, "sin"), (2, 0.22, "cos"), (3, 0.07, "sin")],
    "vox_ah": series(range(1, 13), _formant),
    "vox_oo": [(1, 1.0, "sin"), (2, 0.4, "cos"), (3, 0.12, "sin"), (4, 0.04, "cos")],
    "brass": series(range(1, 13), lambda n: n * math.exp(-n / 2.5)),              # peak at 2-3
    "bowpos": series(range(1, 17), lambda n: math.sin(math.pi * n / 9) / n),      # bowed near the bridge
    "pick_bridge": series(range(1, 19), lambda n: math.sin(math.pi * n / 10) / n),  # bright pick
    "pick_mid": series(range(1, 14), lambda n: math.sin(math.pi * n / 4) / (n * n)),  # warm pluck
}


# ------------------------------------------------------------------ behaviours

def b_pluck(a):
    lo, hi = split(a, 2)
    return make(lo, hi, peak=_peak(a), att=rise(1), dec=coef(700), rel=coef(80),
                bp0=1.0, bdec=coef(180), r2=0.15, b2=0.85)


def b_keys(a):
    lo, hi = split(a, 2)
    return make(lo, hi, peak=_peak(a), acc=0.6, att=rise(2), dec=coef(2600), rel=coef(250),
                bp0=1.0, bdec=coef(500), ma=0.0, mb=1.0, r2=0.3, b2=0.9)


def b_pad(a):
    return make(a, a, reads=2.0, att=rise(350), rel=coef(900), r2=1.0,
                pa=R(0.996), pc=R(1.004), **FREE)


def b_brass(a):
    lo, hi = split(a, 2)
    return make(lo, hi, peak=_peak(a), att=rise(40), rel=coef(120),
                bp0=1.0, bdec=coef(1500), ma=coef(120), mb=1.0 - coef(120),
                r2=0.35, q2=1.0, **vib(5.0, 0.004))


def b_bowed(a):
    return make(a, a, reads=1.8, att=rise(140), rel=coef(350), r2=0.8,
                **FREE, **vib(5.5, 0.005, 1.0, 1.0035))


def b_mallet(a):
    return make(a, [(1, 1.0, "sin")], att=rise(0.5), dec=coef(900), rel=coef(250),
                bp0=1.0, bdec=coef(70), b3=0.8, sh=2)


def b_lead(a):
    return make(a, pre=0.6, att=rise(8), rel=coef(100), e=0.06, dcblock=True,
                **vib(5.5, 0.006))


BEHAVIOURS = {
    "pluck": b_pluck, "keys": b_keys, "pad": b_pad, "brass": b_brass,
    "bowed": b_bowed, "mallet": b_mallet, "lead": b_lead,
}


# ------------------------------------------------------------------ named emulations

_S = [(1, 1.0, "sin")]


def _named() -> dict:
    A = ATOMS
    pick7 = series(range(1, 15), lambda n: math.sin(math.pi * n / 7) / n)
    pick8 = series(range(1, 17), lambda n: math.sin(math.pi * n / 8) / n)
    pick14 = series(range(1, 17), lambda n: math.sin(math.pi * n / 14) / n)
    pick9 = series(range(1, 17), lambda n: math.sin(math.pi * n / 9) / n)
    saw12 = upto(A["saw"], 12)
    reed = upto(A["pulse25"], 12)
    harp_lo, harp_hi = split(A["pick_mid"], 2)
    harmonica = [(1, 1.0, "sin"), (2, 0.5, "cos"), (3, 0.6, "sin"), (4, 0.25, "cos"),
                 (5, 0.3, "sin"), (6, 0.1, "cos"), (7, 0.12, "sin")]
    k, b = "keys", "bass"
    return {
        # mallets and bells: a sine at the note (octaves up) + an inharmonic
        # partial on HIGH that dies fast
        "marimba": (k, *make(_S, _S, pa=R(2), pc=R(2 * 3.93), att=rise(0.5), dec=coef(600),
                             rel=coef(400), bp0=1.0, bdec=coef(90), b2=0.5)),
        "vibes": (k, *make(_S, _S, reads=1.3, pa=R(2), pc=R(2 * 4.0), att=rise(0.5),
                           dec=coef(4000), rel=coef(800), bp0=1.0, bdec=coef(300),
                           r2=0.1, b2=0.5, **trem(5.0, 0.5))),
        "glock": (k, *make(_S, _S, reads=1.4, pa=R(4), pc=R(4 * 2.76), att=rise(0.3),
                           dec=coef(2200), rel=coef(1500), bp0=1.0, bdec=coef(400),
                           r2=0.25, b2=0.4)),
        "musicbox": (k, *make(_S, _S, reads=1.3, pa=R(4), pc=R(4 * 5.95), att=rise(0.3),
                              dec=coef(1400), rel=coef(900), bp0=1.0, bdec=coef(150), b2=0.6)),
        "kalimba": (k, *make(_S, _S, reads=1.3, pa=R(2), pc=R(2 * 5.4), att=rise(0.5),
                             dec=coef(1100), rel=coef(600), bp0=1.0, bdec=coef(40), b2=0.6)),
        "bell": (k, *make([(1, 0.6, "sin"), (2, 1.0, "sin"), (3, 0.5, "sin"), (4, 0.7, "sin"),
                           (5, 0.25, "sin")], _S, reads=1.5,
                          pa=R(0.5), pc=R(1.2), att=rise(0.5), dec=coef(5000), rel=coef(3000),
                          bp0=1.0, bdec=coef(1500), r2=0.6, b2=0.4)),
        "steelpan": (k, *make([(1, 1.0, "sin"), (2, 0.7, "cos"), (3, 0.25, "sin")], _S,
                              reads=1.3, pa=R(2), pc=R(2 * 2.016), att=rise(0.5),
                              dec=coef(1200), rel=coef(500), bp0=1.0, bdec=coef(200), b2=0.5)),
        # plucked strings
        "clav": (k, *make(*split(pick7, 3), pre=0.35, peak=_peak(pick7), att=rise(0.5),
                          dec=coef(700), rel=coef(25), bp0=1.0, bdec=coef(250), r2=0.4, b2=0.6,
                          e=0.06, dcblock=True)),
        "harpsichord": (k, *make(pick8, upto(A["saw"], 6), reads=1.5, att=rise(0.3),
                                 dec=coef(2500), rel=coef(150), r2=0.5,
                                 pc=R(2.003), **FREE)),
        "nylon": (k, *make(harp_lo, harp_hi, peak=_peak(A["pick_mid"]), att=rise(1),
                           dec=coef(1600), rel=coef(200), bp0=1.0, bdec=coef(250),
                           r2=0.3, b2=0.7)),
        "12string": (k, *make(pick7, upto(pick7, 6), reads=1.45, att=rise(1), dec=coef(2200),
                              rel=coef(250), r2=0.45, pc=R(2.004), **FREE)),
        "banjo": (k, *make(*split(pick14, 3), peak=_peak(pick14), att=rise(0.5), dec=coef(450),
                           rel=coef(120), bp0=1.0, bdec=coef(80), r2=0.3, b2=0.8,
                           e=0.05, dcblock=True)),
        "sitar": (k, *make(*split(A["pick_bridge"], 3), peak=_peak(A["pick_bridge"]),
                           att=rise(0.5), dec=coef(2500), rel=coef(300), bp0=1.0,
                           bdec=coef(1200), ma=coef(150), mb=1.0 - coef(150), r2=0.1, q2=1.2)),
        # reeds and winds
        "accordion": (k, *make(reed, reed, reads=1.8, att=rise(25), rel=coef(60), r2=0.8,
                               pc=R(1.006), e=0.03, dcblock=True, **FREE)),
        "harmonica": (k, *make(harmonica, pre=0.3, att=rise(30), rel=coef(70), e=0.03,
                               dcblock=True, **trem(5.5, 0.3))),
        "clarinet": (k, *make(*split(A["hollow"], 1), peak=_peak(A["hollow"]), att=rise(40),
                              rel=coef(90), bp0=1.0, bdec=coef(1200), ma=coef(150),
                              mb=1.0 - coef(150), r2=0.6, q2=0.5)),
        "oboe": (k, *make(A["pulse12"], att=rise(30), rel=coef(90), **vib(5.0, 0.004))),
        "flute": (k, *make(A["flute"], _S, att=rise(80), rel=coef(120), bp0=1.0,
                           bdec=coef(60), b3=0.3, sh=2, **vib(5.0, 0.005))),
        # brass, strings, voices
        "trumpet": (k, *make(*split(A["brass"], 2), pre=0.2, peak=_peak(A["brass"]),
                             att=rise(30), rel=coef(80), bp0=1.0, bdec=coef(1500),
                             ma=coef(90), mb=1.0 - coef(90), r2=0.35, q2=1.0, e=0.02,
                             dcblock=True, **vib(5.5, 0.0035))),
        "synthbrass": (k, *make(*split(A["saw"], 2), peak=_peak(A["saw"]), att=rise(80),
                                rel=coef(200), bp0=1.0, bdec=coef(2000), ma=coef(250),
                                mb=1.0 - coef(250), r2=0.3, q2=0.9)),
        "strings": (k, *make(saw12, upto(A["saw"], 8), reads=1.8, att=rise(180), rel=coef(500),
                             r2=0.8, **FREE, **vib(5.5, 0.005, 1.0, 1.004))),
        "cello": (k, *make(A["bowpos"], att=rise(90), rel=coef(250), **vib(5.0, 0.007, 0.5))),
        "choir": (k, *make(A["vox_ah"], A["vox_ah"], reads=1.9, att=rise(250), rel=coef(600),
                           r2=0.9, **FREE, **vib(5.0, 0.0035, 1.0, 1.0045))),
        "voice_oo": (k, *make(A["vox_oo"], att=rise(150), rel=coef(400), **vib(5.0, 0.004))),
        # basses
        "synthbass": (b, *make(*split(A["saw"], 2), pre=0.3, peak=_peak(A["saw"]), att=rise(1),
                               dec=coef(900), rel=coef(40), bp0=1.0, bdec=coef(200),
                               r2=0.2, b2=1.0, e=0.03, dcblock=True)),
        "acidbass": (b, *make(*split(A["square"], 2), pre=0.5, peak=_peak(A["square"]),
                              att=rise(1), dec=coef(600), rel=coef(40), bp0=1.0,
                              bdec=coef(120), r2=0.1, b2=1.2, e=0.08, dcblock=True)),
        "upright": (b, *make([(1, 1.0, "sin"), (2, 0.45, "cos"), (3, 0.15, "sin")],
                             series(range(3, 9), lambda n: 1.0 / n), att=rise(3),
                             dec=coef(1600), rel=coef(70), bp0=1.0, bdec=coef(50),
                             r2=0.05, b2=0.8)),
        "sub808": (b, *make(_S, _S, pre=0.5, att=rise(1), dec=coef(1800), rel=coef(80),
                            bp0=1.0, bdec=coef(10), b3=0.5, sh=3, e=0.02, dcblock=True)),
        "fretless": (b, *make([(1, 1.0, "sin"), (2, 0.3, "cos")],
                              [(3, 0.2, "sin"), (4, 0.06, "cos"), (5, 0.05, "sin")],
                              att=rise(12), dec=coef(3500), rel=coef(80), bp0=1.0,
                              bdec=coef(1500), ma=coef(120), mb=1.0 - coef(120),
                              r2=0.3, q2=1.0, **vib(4.5, 0.006))),
        "slapbass": (b, *make(*split(pick9, 2), peak=_peak(pick9), att=rise(0.5),
                              dec=coef(800), rel=coef(60), bp0=1.0, bdec=coef(60),
                              r2=0.25, b2=1.3, e=0.06, dcblock=True)),
    }


NAMED = _named()


def candidate(name: str) -> tuple:
    """(role, LOW, HIGH, REC) of "named_<name>" or "grid_<behaviour>_<atom>"."""
    if name.startswith("named_"):
        return NAMED[name[6:]]
    beh, atom = name[5:].split("_", 1)
    return ("keys", *BEHAVIOURS[beh](ATOMS[atom]))


def names() -> list:
    return ([f"named_{n}" for n in NAMED]
            + [f"grid_{b}_{a}" for b in BEHAVIOURS for a in ATOMS])
