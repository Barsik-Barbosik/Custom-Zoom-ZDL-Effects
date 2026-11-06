"""The one SongBox instrument engine: a wavetable voice driven by a record
(instruments/<id>.py: ROLE, LEVEL, LOW, HIGH, REC). Mirror of engine.c (keep
in sync, line by line where it can be).

Wavetable: one cycle, WT_N + 1 uint32 words per instrument, built in ctx[3].
Word i: low half = sum of the LOW terms (signed int16), high half = sum of
the HIGH terms + WT_OFS (so the word reads as a tiny float). A term (h, amp,
"sin" | "cos") = amp x sin / cos of harmonic h at phase i / WT_N, from the
parabolic sine psin (the table's harmonic 1 is the note). The build scales
LOW to a peak of TSO_PEAK and HIGH to TSC_PEAK (table units).

One sample (C: three loops over a piece, see engine.c):
  ip3 += rot                                  LFO phase (2^32 = a cycle)
  tf = |ip3 / 2 - 2^30|                       triangle 0..2^30
  ip += inc (pa + pb tf), ip2 += inc (pc + pb tf)
  lp2 *= dec;  env = env (rel + k (1 - att - rel)) + k att lp2   (k = gate open)
  bp *= bdec;  menv = menv ma + bp mb
  s0 = env (g0 + g1 menv)
  x = s0 low(ip) + s0 (r2 + q2 menv + b2 bp) high(ip2) + s0 b3 bp high(ip << sh)
      (low, high(ip2): linear between words; high(ip << sh): nearest word)
  c = 2 clamp1(x);  y = 0.75 c + c^2 (e - 0.0625 c)   soft clip (+ even part)
  out = (y - lp) p3;  lp = lp la + y dc               DC blocker (la = 0: off)
  p3 = (t0 + ts sin(ip3) + tt tri(ip3)) post x level  (C: once per piece)
A new note: lp2 = 1, bp = bp0, menv = menv mk + m0, ip2 = ip2 r0 + ip m2.

REC fields are in signal units: g0, g1 per unit of LOW's sum, r2 / q2 / b2 /
b3 per unit of HIGH's sum relative to LOW's; record() folds the table
scales in.
"""

from __future__ import annotations

from inst_lib import WT_BITS, WT_N, WT_OFS, coef, psin, rise

TWO32 = 4294967296.0
MASK = 0xFFFFFFFF
TSO_PEAK = 30000.0          # LOW's peak in table units (int16)
TSC_PEAK = 6400.0           # HIGH's peak (+ WT_OFS 6464: 64..12864, a tiny float)

# C struct SsInst field order (engine.c); the last four are uint32
FLOATS = ("pa", "pb", "pc", "att", "dec", "rel", "bdec", "ma", "mb", "g0", "g1",
          "r2", "q2", "b2", "b3", "e", "la", "dc", "t0", "ts", "tt", "post",
          "bp0", "mk", "m0")
INTS = ("rot", "sh", "r0", "m2")

# a plain instrument: note at ip, no second / third read, no LFO, no clip
# colour, no DC blocker; organ-like envelope
DEFAULTS = dict(pa=TWO32, pb=0.0, pc=TWO32, att=rise(4), dec=1.0, rel=coef(60),
                bdec=1.0, ma=1.0, mb=0.0, g0=1.0, g1=0.0, r2=0.0, q2=0.0, b2=0.0,
                b3=0.0, e=0.0, la=0.0, dc=0.0, t0=1.0, ts=0.0, tt=0.0, post=1.0,
                bp0=0.0, mk=0.0, m0=0.0, rot=0, sh=0, r0=0, m2=1)


def term_word(h: int, amp: int, cos: bool) -> int:
    """C ss_wt_terms word: amp (int16) << 16 | cos << 10 | h."""
    assert 1 <= h < 1024 and -32768 <= amp < 32768
    return (amp & 0xFFFF) << 16 | int(cos) << 10 | h


def term_val(i: int, w: int) -> float:
    """One term of row i (C ss_wt_term)."""
    h = w & 0x3FF
    ph = (h * i + ((w >> 10) & 1) * (WT_N // 4)) & (WT_N - 1)
    amp = (w >> 16) - (0x10000 if w & 0x80000000 else 0)
    return amp * psin(ph * (1.0 / WT_N))


def _peak(terms) -> float:
    if not terms:
        return 0.0
    words = [term_word(h, 1, kind == "cos") for h, _a, kind in terms]
    amps = [a for _h, a, _k in terms]
    return max(abs(sum(a * term_val(i, w) for a, w in zip(amps, words))) for i in range(WT_N))


def scaled_terms(terms, peak_units: float) -> tuple:
    """(term words, scale): amplitudes x scale as int16, sum peak = peak_units."""
    pk = _peak(terms)
    sc = peak_units / pk if pk else 1.0
    return [term_word(h, int(round(a * sc)), kind == "cos") for h, a, kind in terms], sc


def table(lw: list, hw: list) -> list:
    """WT_N + 1 table words from the scaled terms (C ss_wt_build)."""
    out = []
    for i in range(WT_N + 1):
        a = 0.0
        for w in lw:
            a += term_val(i, w)
        b = 0.0
        for w in hw:
            b += term_val(i, w)
        out.append(((int(a + 32768.5) - 32768) & 0xFFFF) | int(b + (WT_OFS + 0.5)) << 16)
    return out


class Instrument:
    """An instrument file's record + table, ready for render() and the C header."""

    def __init__(self, mod):
        rec = dict(DEFAULTS)
        for k in mod.REC:
            if k not in rec:
                raise ValueError(f"{mod.__name__}: REC field {k!r} unknown")
        rec.update(mod.REC)
        self.low, tso = scaled_terms(mod.LOW, TSO_PEAK)
        self.high, tsc = scaled_terms(getattr(mod, "HIGH", []), TSC_PEAK)
        rec["g0"] /= tso
        rec["g1"] /= tso
        for k in ("r2", "q2", "b2", "b3"):
            rec[k] *= tso / tsc
        for k in INTS:
            rec[k] = int(rec[k]) & MASK
        self.rec = rec
        self._t = None

    def t(self) -> list:
        if self._t is None:
            self._t = table(self.low, self.high)
        return self._t

    def render(self, v) -> float:
        """One sample of voice v (inst_lib.Voice; v.att < 1: a new note)."""
        r, t = self.rec, self.t()
        if v.att < 1.0:
            v.att = 1.0
            v.lp2 = 1.0
            v.bp = r["bp0"]
            v.menv = v.menv * r["mk"] + r["m0"]
            v.ip2 = (v.ip2 * r["r0"] + v.ip * r["m2"]) & MASK
        v.ip3 = (v.ip3 + r["rot"]) & MASK
        tf = float(abs((v.ip3 >> 1) - 0x40000000))
        v.ip = (v.ip + int(v.f * (r["pa"] + r["pb"] * tf))) & MASK
        v.ip2 = (v.ip2 + int(v.f * (r["pc"] + r["pb"] * tf))) & MASK
        k = 1.0 if v.gate > 0 else 0.0
        v.lp2 *= r["dec"]
        v.env = v.env * (r["rel"] + k * (1.0 - r["att"] - r["rel"])) + k * r["att"] * v.lp2
        v.bp *= r["bdec"]
        v.menv = v.menv * r["ma"] + v.bp * r["mb"]
        s0 = v.env * (r["g0"] + r["g1"] * v.menv)
        s1 = s0 * (r["r2"] + r["q2"] * v.menv + r["b2"] * v.bp)
        s2 = s0 * r["b3"] * v.bp
        sh = 32 - WT_BITS
        fm = (1 << sh) - 1
        j, f = v.ip >> sh, (v.ip & fm) * (1.0 / (1 << sh))
        oa = (t[j] & 0xFFFF) - (0x10000 if t[j] & 0x8000 else 0)
        ob = (t[j + 1] & 0xFFFF) - (0x10000 if t[j + 1] & 0x8000 else 0)
        j2, f2 = v.ip2 >> sh, (v.ip2 & fm) * (1.0 / (1 << sh))
        ca, cb = t[j2] >> 16, t[j2 + 1] >> 16
        j3 = ((v.ip << r["sh"]) & MASK) >> sh
        x = s0 * (oa + (ob - oa) * f) + s1 * (ca + (cb - ca) * f2 - WT_OFS) + s2 * ((t[j3] >> 16) - WT_OFS)
        c = abs(x + 1.0) - abs(x - 1.0)
        y = 0.75 * c + c * c * (r["e"] - 0.0625 * c)
        u = v.ip3
        p3 = (r["t0"] + r["ts"] * psin((u >> 8) * (1.0 / 16777216.0))
              + r["tt"] * abs((u >> 1) - 0x40000000) * (1.0 / 1073741824.0)) * r["post"]
        o = (y - v.lp) * p3
        v.lp = v.lp * r["la"] + y * r["dc"]
        return o
