"""Jazz organ (keys): drawbars 888000000 (16', 5 1/3', 8'), 2 2/3'
percussion, key click, no vibrato. One sine at the 16' phase; the
drawbar harmonics from it (Chebyshev: s = sin x, h2 = cos 2x = 1 - 2 s^2,
h3 = sin 3x = s (3 - 4 s^2)). Also plays its set's bass, unchanged.
Wavetable (TABLE): low half the drawbar sum s + h2 + h3, high half h2;
the percussion h6 = cos 6x and the click h32 = cos 32x are h2 read at 3x
and 16x the phase (integer phase ip x 3, x 16: wrap by themselves).
C: organ_jazz.c (keep in sync)."""

from inst_lib import WT_BITS, WT_N, WT_OFS, coef, psin, rise

ROLE = "keys"
LEVEL = 0.063
TABLE = True
CONST = dict(att=rise(4), rel=coef(60), perc=coef(250), click=coef(4),
             tso=15000.0, tsc=6400.0,   # table scales: |s + h2 + h3| <= 2.04, |h2| <= 1
             rc=15000.0 / 6400.0)       # high half in table units of the low half


def table_word(s: float) -> int:
    """Wavetable word for s = psin(i / WT_N): low half the drawbar sum, high
    half h2."""
    c = CONST
    s2 = s * s
    h2 = 1.0 - 2.0 * s2
    h3 = s * (3.0 - 4.0 * s2)
    o = s + h2 + h3
    return ((int(o * c["tso"] + 32768.5) - 32768) & 0xFFFF) | int(h2 * c["tsc"] + (WT_OFS + 0.5)) << 16


_T = []


def _hi(ph: int, lin: bool = True) -> float:
    """High half (h2) at integer phase ph, linear between words (or the
    nearest word below), - WT_OFS."""
    sh = 32 - WT_BITS
    j = ph >> sh
    f = (ph & ((1 << sh) - 1)) * (1.0 / (1 << sh)) if lin else 0.0
    ca, cb = _T[j] >> 16, _T[j + 1] >> 16
    return ca + (cb - ca) * f - WT_OFS


def render(v) -> float:
    c = CONST
    if not _T:
        _T.extend(table_word(psin(i * (1.0 / WT_N))) for i in range(WT_N + 1))
    if v.att < 1.0:
        v.att = 1.0                                 # menv: percussion, bp: click, in table units
        v.menv = c["rc"] * 0.8; v.bp = c["rc"] * 0.25
    v.ip = (v.ip + int(v.f * 4294967296.0)) & 0xFFFFFFFF   # 16' phase at f (the 16' sounds the note), 2^32 = a cycle
    k = 1.0 if v.gate > 0 else 0.0
    v.env = v.env * (c["rel"] + k * (1.0 - c["att"] - c["rel"])) + k * c["att"]
    v.menv *= c["perc"]
    v.bp *= c["click"]
    a = v.env * (1.0 / c["tso"])
    pc = a * v.menv                                 # percussion level
    kc = a * v.bp                                   # click level
    sh = 32 - WT_BITS
    j = v.ip >> sh
    f = (v.ip & ((1 << sh) - 1)) * (1.0 / (1 << sh))
    oa, ob = (_T[j] & 0xFFFF) ^ 0x8000, (_T[j + 1] & 0xFFFF) ^ 0x8000
    oa, ob = oa - 0x8000, ob - 0x8000                  # signed low halves
    h6 = _hi((v.ip * 3) & 0xFFFFFFFF)
    h32 = _hi((v.ip << 4) & 0xFFFFFFFF, False)     # a 4 ms click: nearest word
    return (a * (oa + (ob - oa) * f) + kc * h32) + pc * h6
