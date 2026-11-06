"""EPiano (keys), shimmering Wurlitzer-style reed: two reeds a few cents apart
(slow beating) + a little 2nd harmonic into an asymmetric cubic soft clip
(harder at the strike), DC blocked, fast tremolo (6.5 Hz, 30 %). Also plays
its set's bass, unchanged. Reed 1 + its 2nd harmonic and reed 2 are one
cycle in a wavetable (TABLE: low half reed 1 + harmonic, high half reed 2,
each read at its own integer phase: ip, ip2). Every envelope moves
smoothly (no jump at a new note, even over a ringing one): the level env
rises (att) to the slow decay lp2, the strike menv rises to bp, which
decays. The C code works out the tremolo once per piece (PIECE, ss_p_,
<= 8 samples, held over the piece); here it runs per sample (within 0.2 %).
C: epiano.c (keep in sync)."""

from inst_lib import SR, WT_BITS, WT_N, WT_OFS, coef, lp_coef, psin, rise

ROLE = "keys"
LEVEL = 0.18
TABLE = True
PIECE = True                    # C: ss_p_epiano once per piece (tremolo)
CONST = dict(att=rise(15), dec=coef(3500), rel=coef(140), bdec=coef(400), dc=lp_coef(20.0),
             trem=6.5 / SR,
             tso=30000.0, tsc=6400.0,   # table scales: |0.6 s + 0.3 h2| <= 0.9, |s| <= 1
             rb=0.6 * 30000.0 / 6400.0)  # reed 2 (0.6 s) in table units of the low half


def table_word(s: float) -> int:
    """Wavetable word for s = psin(i / WT_N): low half reed 1 + 2nd harmonic
    (h2 = cos 2x = 1 - 2 s^2), high half reed 2."""
    c = CONST
    o = 0.6 * s + 0.3 * (1.0 - 2.0 * s * s)
    return ((int(o * c["tso"] + 32768.5) - 32768) & 0xFFFF) | int(s * c["tsc"] + (WT_OFS + 0.5)) << 16


_T = []


def render(v) -> float:
    c = CONST
    if not _T:
        _T.extend(table_word(psin(i * (1.0 / WT_N))) for i in range(WT_N + 1))
    if v.att < 1.0:
        v.att = 1.0; v.lp2 = 1.0; v.bp = 1.0
    v.ip = (v.ip + int(v.f * 4294967296.0)) & 0xFFFFFFFF            # integer phases: 2^32 = a cycle
    v.ip2 = (v.ip2 + int(v.f * (1.003 * 4294967296.0))) & 0xFFFFFFFF  # second reed
    v.ip3 = (v.ip3 + int(c["trem"] * 4294967296.0)) & 0xFFFFFFFF
    k = 1.0 if v.gate > 0 else 0.0
    v.lp2 *= c["dec"]                                   # slow decay (1 at a new note)
    v.env = v.env * (c["rel"] + k * (1.0 - c["att"] - c["rel"])) + k * c["att"] * v.lp2
    v.bp *= c["bdec"]                                   # strike (1 at a new note)
    v.menv = v.menv * (1.0 - c["att"]) + v.bp * c["att"]
    a = v.env * (0.55 + 0.9 * v.menv) * (1.0 / c["tso"])
    tr = 0.85 - 0.15 * psin((v.ip3 >> 8) * (1.0 / 16777216.0))   # tremolo
    sh = 32 - WT_BITS
    j = v.ip >> sh
    f = (v.ip & ((1 << sh) - 1)) * (1.0 / (1 << sh))
    oa, ob = (_T[j] & 0xFFFF) ^ 0x8000, (_T[j + 1] & 0xFFFF) ^ 0x8000
    oa, ob = oa - 0x8000, ob - 0x8000                  # signed low halves
    j = v.ip2 >> sh
    f2 = (v.ip2 & ((1 << sh) - 1)) * (1.0 / (1 << sh))
    ca, cb = _T[j] >> 16, _T[j + 1] >> 16
    x = a * (oa + (ob - oa) * f + c["rb"] * (ca + (cb - ca) * f2 - WT_OFS))
    x = 0.5 * (abs(x + 1.0) - abs(x - 1.0))         # clamp to -1..1 (C: ss_clamp1)
    y = 1.5 * x - 0.5 * x * x * x + 0.15 * x * x    # soft clip + even harmonic
    v.lp = v.lp * (1.0 - c["dc"]) + y * c["dc"]     # DC
    return (y - v.lp) * tr
