"""Rock organ (keys): most drawbars out (16', 8', 5 1/3', 4', 2 2/3', 2',
1') from one 16' sine (Chebyshev, see organ_jazz.py; h4 = 2 h2^2 - 1,
h8 = 2 h4^2 - 1, h16 = 2 h8^2 - 1), key click (h32 = 2 h16^2 - 1), driven into a soft clip,
fast rotary (6.7 Hz triangle: pitch + level). The drawbar sum and the click
are one cycle in a wavetable (TABLE: built once, read linearly between
words). C: organ_rock.c (keep in sync)."""

from inst_lib import SR, WT_BITS, WT_N, WT_OFS, clip, coef, psin, rise

ROLE = "keys"
LEVEL = 0.126 / 1.1     # without sets.MASTER's +10 % (the others have it)
TABLE = True
CONST = dict(att=rise(4), rel=coef(60), click=coef(4), rot=6.7 / SR,
             tso=6900.0, tsc=6400.0,   # table scales: |sum| <= 4.7, |h32| <= 1
             clk0=0.3 * 6900.0 / 6400.0)   # click start level, in table units of the sum


def table_word(s: float) -> int:
    """Wavetable word for s = psin(i / WT_N): low half the drawbar sum, high
    half the click."""
    c = CONST
    s2 = s * s
    h2 = 1.0 - 2.0 * s2
    h3 = s * (3.0 - 4.0 * s2)
    h4 = 2.0 * h2 * h2 - 1.0
    h6 = h2 * (4.0 * h2 * h2 - 3.0)
    h8 = 2.0 * h4 * h4 - 1.0
    h16 = 2.0 * h8 * h8 - 1.0
    o = 0.8 * s + h2 + 0.8 * h3 + 0.7 * h4 + 0.5 * h6 + 0.5 * h8 + 0.4 * h16
    k = 2.0 * h16 * h16 - 1.0                           # click: h32
    return ((int(o * c["tso"] + 32768.5) - 32768) & 0xFFFF) | int(k * c["tsc"] + (WT_OFS + 0.5)) << 16


_T = []


def render(v) -> float:
    c = CONST
    if not _T:
        _T.extend(table_word(psin(i * (1.0 / WT_N))) for i in range(WT_N + 1))
    v.ip3 = (v.ip3 + int(c["rot"] * 4294967296.0)) & 0xFFFFFFFF   # integer phases: 2^32 = a cycle
    tf = float(abs((v.ip3 >> 1) - 0x40000000))         # rotary triangle, 0..2^30
    v.ip = (v.ip + int(v.f * (0.997 * 4294967296.0) + v.f * 0.024 * tf)) & 0xFFFFFFFF   # f (16' sounds the note), +-0.3 %
    if v.att < 1.0:
        v.att = 1.0; v.bp = c["clk0"]
    k = 1.0 if v.gate > 0 else 0.0
    v.env = v.env * (c["rel"] + k * (1.0 - c["att"] - c["rel"])) + k * c["att"]
    v.bp *= c["click"]
    sh = 32 - WT_BITS
    j = v.ip >> sh
    f = (v.ip & ((1 << sh) - 1)) * (1.0 / (1 << sh))
    w0, w1 = _T[j], _T[j + 1]
    oa, ob = (w0 & 0xFFFF) ^ 0x8000, (w1 & 0xFFFF) ^ 0x8000
    oa, ob = oa - 0x8000, ob - 0x8000                  # signed low halves
    ca, cb = w0 >> 16, w1 >> 16
    x = (v.env * (0.45 / c["tso"])) * (oa + (ob - oa) * f + v.bp * (ca + (cb - ca) * f - WT_OFS))
    return clip(x) * (1.0 - (0.4 / 2147483648.0) * tf)
