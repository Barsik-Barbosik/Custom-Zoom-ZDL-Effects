"""C-style model of the song packer / unpacker (mirrored by songpack.h).

Same stream format as songseq.py (the reference model), written the way the
C will be: fixed-size arrays, integers only, no division or modulo in the
hot paths, back pointers instead of token lists, and the work split into
steps (one prep row or one DP row per step) so the DSP can run it across
audio calls.

Bar = one byte: chord (root * 2 + minor, 0..23; 24 = rest) | rhythm << 5
(a rest is always 24: it plays in the rhythm state it falls in).

Packer moves from bar position p (DP over positions, per key):
  BAR    bars[p] as tokens: NEXT / PREV, FLAT, SWITCH, degree     3..21 bits
  REP3   bars[p..p+2] == bars[p-1]                                6 bits
  SECT r bars[p..] == the last rhythm-r section before p          9 bits
  COPY   bars[p..j) repeats bars[p-N..p), j - p = c * N, c 1..8  15 bits
More Floppies (parts = 1..4): the stream goes on, 34 tokens per part. In
words[] part q takes bits 128 q .. 128 q + 105 (4 words each, as zmenu moves
them); its first 4 bits are the key (part 1) or the part marker (extra
copies, written by zmenu, skipped here), then its 34 tokens.
Settings (SongBox MENU OCTAVE + LENGTH, a code 0..8; 0 = defaults) first in
the stream: code 0..6 one token, 7 / 8 token 7 + 0 / 1 (7 + 2..7 read as 0,
free for later); start(pre = code).
COPY and SECT targets don't depend on the key: the prep rows find them once
(copy table: 128 x 128 bytes, N per target). The reference model allows REP3
only inside a run it started itself; here REP3 may follow any bar, so a
result can be cheaper than the reference's, never dearer.

python -B songpack.py              compare with songseq.py (1..4 parts), write vectors
python -B songpack.py --quick      fewer songs
"""
from __future__ import annotations

import random
import sys
from pathlib import Path

import songseq as ref

HERE = Path(__file__).resolve().parent

CAP_BITS = 106
KEY_BITS = 4
NTOK = 34                     # (106 - 4) / 3 tokens per part (Floppy)
BUDGET = NTOK * 3             # 102 token bits per part
MAX_PARTS = 4                 # Floppies: G1on chain of 5 = SongBox + 4
NWORDS = 4 * MAX_PARTS        # part q in words[4 q .. 4 q + 3]
MAX_BARS = 128
COPY_MAX_N = 64
COPY_MAX_C = 8
INF = 0xFFFF
B_REST = 24

EV = 0
E_END, E_SWITCH, E_FLAT, E_NEXT, E_PREV, E_REP3, E_COPY, E_SECT = range(8)
REST_TOK = 4
SET_ESC = 7                   # settings token 7: escape, one more token follows
SET_N = 9                     # settings codes 0..8

BITS_EV = 6
BITS_REP3 = 6
BITS_SECT = 9
BITS_COPY = 15
BITS_REST = 9

DEG_OFS = (0, 2, 4, 5, 7, 9, 10)
DEG_MIN = (0, 1, 1, 0, 0, 1, 0)

# chord relative to the key (semitones 0..11) -> token | flat << 3 | default minor << 4
REL_CODE = [0] * 12
for _rel in range(12):
    if _rel in DEG_OFS:
        _d = DEG_OFS.index(_rel)
        REL_CODE[_rel] = (_d + 1) | DEG_MIN[_d] << 4
    else:
        _d = DEG_OFS.index((_rel + 1) % 12)
        REL_CODE[_rel] = (_d + 1) | 1 << 3

# move codes in op[]: type << 8 | arg
M_BAR, M_REP3, M_SECT, M_COPY = 1, 2, 3, 4


def bar_rhythm(b: int) -> int:
    return b >> 5


def bar_chord(b: int) -> int:
    return b & 31


def mod12(x: int) -> int:
    """x in -12..23 -> 0..11 (no %)."""
    if x < 0:
        x += 12
    if x >= 12:
        x -= 12
    return x


def next_addr(a: int) -> int:
    """Bit address of the token after the one at a (parts are 128 bits apart,
    tokens fill bits 4..105 of each)."""
    a += 3
    if (a & 127) == 106:
        a += 26
    return a


def words_hash(words: list, parts: int) -> int:
    """Self-test check value of words[0 .. 4 * parts)."""
    h = 0
    for i in range(4 * parts):
        h = (h * 31 + words[i]) & 0xFFFFFFFF
    return h


def bar_bits(key: int, b: int, beat: int) -> int:
    ch = b & 31
    if ch == B_REST:
        return BITS_REST
    bits = 3
    if (b >> 5) != beat:
        bits += BITS_EV
    code = REL_CODE[mod12((ch >> 1) - key)]
    if code & 8:
        bits += BITS_EV
    if (ch & 1) != (code >> 4):
        bits += BITS_EV
    return bits


class Packer:
    """All state in fixed arrays (ctx[3] in C). start(), then step() until it
    returns True; result in key / bits / words."""

    def __init__(self):
        self.bars = [0] * MAX_BARS
        self.rh = [0] * MAX_BARS            # rhythm state after bar i (beat_at(i + 1))
        self.sect = [0] * (MAX_BARS * 3)    # SECT r at p: section length, 0 = none
        self.copy = [0] * (MAX_BARS * MAX_BARS)  # [p * 128 + (j - p - 1)] = N, 0 = none
        self.sec_s = [0] * 3
        self.sec_e = [0] * 3
        self.run_n = [0] * (COPY_MAX_N + 1)
        self.cost = [[INF] * (MAX_BARS + 1) for _ in range(2)]
        self.frm = [[0] * (MAX_BARS + 1) for _ in range(2)]
        self.op = [[0] * (MAX_BARS + 1) for _ in range(2)]
        self.tok = [0] * (NTOK * MAX_PARTS)
        self.stack = [0] * (MAX_BARS + 1)
        self.n = 0
        self.ops = 0                        # inner-loop work counter (model only)
        self.row_max = 0

    # -------------------------------------------------------------- driver

    def start(self, bars: list, n: int, parts: int = 1, pre: int = 0) -> None:
        for i in range(n):
            self.bars[i] = bars[i]
        self.n = n
        self.parts = parts
        self.pre = pre                      # settings code 0..8
        self.budget = BUDGET * parts        # token bits of all parts
        self.phase = 0                      # 0 copy rows (backward), 1 rhythm / SECT rows,
        self.p = n - 1                      # 2 key DP, 3 done
        self.k = 0
        self.cur = 0
        self.best = -1
        self.bkey = 0
        self.bcost = INF
        for r in range(3):
            self.sec_s[r] = 0
            self.sec_e[r] = 0
        for N in range(COPY_MAX_N + 1):
            self.run_n[N] = 0
        self.ops = 0
        self.row_max = 0

    def step(self) -> bool:
        before = self.ops
        if self.phase == 0:
            if self.p >= 0:
                self.copy_row(self.p)
                self.p -= 1
            else:
                self.phase = 1
                self.p = 0
        elif self.phase == 1:
            if self.p < self.n:
                self.sect_row(self.p)
                self.p += 1
            else:
                self.phase = 2
                self.key_begin()
        elif self.phase == 2:
            if self.p < self.n:
                self.dp_row(self.p)
                self.p += 1
            else:
                self.key_end()
                self.k += 1
                if self.k < 12:
                    self.key_begin()
                else:
                    self.phase = 3
                    self.emit()
        if self.ops - before > self.row_max:
            self.row_max = self.ops - before
        return self.phase == 3

    def run(self, bars: list, parts: int = 1, pre: int = 0) -> "Packer":
        self.start(bars, len(bars), parts, pre)
        steps = 1
        while not self.step():
            steps += 1
        self.steps = steps
        return self

    # -------------------------------------------------------------- prep (key-free)

    def copy_row(self, p: int) -> None:
        """Rows run from the end: run_n[N] = how many bars from p on equal
        the bar N before them (bars[p + i] == bars[p + i - N])."""
        bars = self.bars
        row = p * MAX_BARS
        for i in range(MAX_BARS):
            self.copy[row + i] = 0
        top = p if p < COPY_MAX_N else COPY_MAX_N
        for N in range(1, top + 1):
            self.ops += 1
            if bars[p] == bars[p - N]:
                self.run_n[N] += 1
            else:
                self.run_n[N] = 0
            L = self.run_n[N]
            lim = N << 3
            if L > lim:
                L = lim
            t = N
            while t <= L:                   # every whole repeat is a target
                self.ops += 1
                if self.copy[row + t - 1] == 0:
                    self.copy[row + t - 1] = N
                t += N

    def sect_row(self, p: int) -> None:
        bars, n = self.bars, self.n
        if p > 0:                           # extend / start the section of bar p - 1
            x = self.rh[p - 1]
            if p > 1 and self.rh[p - 2] == x:
                self.sec_e[x] = p
            else:
                self.sec_s[x] = p - 1
                self.sec_e[x] = p
        for r in range(3):                  # SECT r: next bars equal the section?
            s, e = self.sec_s[r], self.sec_e[r]
            ln = 0
            if e > 0 and p + (e - s) <= n:
                ln = e - s
                for i in range(e - s):
                    self.ops += 1
                    if bars[p + i] != bars[s + i]:
                        ln = 0
                        break
            self.sect[p * 3 + r] = ln
        b = bars[p]                         # rhythm state after bar p
        if (b & 31) == B_REST:
            self.rh[p] = self.rh[p - 1] if p > 0 else 0
        else:
            self.rh[p] = b >> 5

    # -------------------------------------------------------------- DP per key

    def key_begin(self) -> None:
        c = self.cost[self.cur]
        for j in range(self.n + 1):
            c[j] = INF
        c[0] = 3 if self.pre < SET_ESC else 6   # settings tokens
        self.p = 0

    def relax(self, j: int, c: int, p: int, op: int) -> None:
        cost = self.cost[self.cur]
        self.ops += 1
        if c <= self.budget and c < cost[j]:
            cost[j] = c
            self.frm[self.cur][j] = p
            self.op[self.cur][j] = op

    def dp_row(self, p: int) -> None:
        c0 = self.cost[self.cur][p]
        if c0 == INF:
            return
        n, bars, k = self.n, self.bars, self.k
        beat = self.rh[p - 1] if p > 0 else 0
        self.relax(p + 1, c0 + bar_bits(k, bars[p], beat), p, M_BAR << 8)
        if p > 0 and p + 3 <= n and self.copy[p * MAX_BARS + 2] == 1:
            # copy table offset 3 holds N = 1 iff bars[p..p+2] == bars[p-1]
            self.relax(p + 3, c0 + BITS_REP3, p, M_REP3 << 8)
        for r in range(3):
            ln = self.sect[p * 3 + r]
            if ln:
                self.relax(p + ln, c0 + BITS_SECT, p, M_SECT << 8 | r)
        row = p * MAX_BARS
        for i in range(n - p):
            N = self.copy[row + i]
            if N:
                self.relax(p + i + 1, c0 + BITS_COPY, p, M_COPY << 8 | N)

    def key_end(self) -> None:
        c = self.cost[self.cur][self.n]
        if c < self.bcost:                  # keep this key's arrays, DP the next in the other
            self.bcost = c
            self.bkey = self.k
            self.best = self.cur
            self.cur ^= 1

    # -------------------------------------------------------------- tokens

    def emit(self) -> None:
        self.fits = self.bcost != INF
        self.ntok = 0
        self.words = [0] * NWORDS           # 106 bits per part, LSB first, as zmenu stores them
        if not self.fits:
            self.bits = INF
            return
        if self.pre < SET_ESC:              # settings first
            self.put(self.pre)
        else:
            self.put(SET_ESC), self.put(self.pre - SET_ESC)
        frm, op = self.frm[self.best], self.op[self.best]
        sp = 0
        j = self.n
        while j > 0:                        # back pointers -> positions, last first
            self.stack[sp] = j
            sp += 1
            j = frm[j]
        key = self.bkey
        while sp > 0:
            sp -= 1
            j = self.stack[sp]
            p = frm[j]
            o = op[j]
            t = o >> 8
            if t == M_BAR:
                b = self.bars[p]
                ch = b & 31
                if ch == B_REST:
                    self.put(EV), self.put(E_FLAT), self.put(REST_TOK)
                else:
                    beat = self.rh[p - 1] if p > 0 else 0
                    d = (b >> 5) - beat
                    if d < 0:
                        d += 3
                    if d == 1:
                        self.put(EV), self.put(E_NEXT)
                    elif d == 2:
                        self.put(EV), self.put(E_PREV)
                    code = REL_CODE[mod12((ch >> 1) - key)]
                    if code & 8:
                        self.put(EV), self.put(E_FLAT)
                    if (ch & 1) != (code >> 4):
                        self.put(EV), self.put(E_SWITCH)
                    self.put(code & 7)
            elif t == M_REP3:
                self.put(EV), self.put(E_REP3)
            elif t == M_SECT:
                self.put(EV), self.put(E_SECT), self.put(o & 7)
            else:
                N = o & 255
                c = 0
                s = 0
                while s < j - p:            # count = (j - p) / N without a divide
                    s += N
                    c += 1
                self.put(EV), self.put(E_COPY)
                self.put((N - 1) & 7), self.put((N - 1) >> 3), self.put(c - 1)
        self.bits = KEY_BITS + 3 * self.ntok
        assert self.bits == KEY_BITS + self.bcost, (self.bits, self.bcost)
        self.put_bits(0, key, KEY_BITS)
        a = KEY_BITS
        for i in range(self.ntok):
            self.put_bits(a, self.tok[i], 3)
            a = next_addr(a)

    def put(self, t: int) -> None:
        self.tok[self.ntok] = t
        self.ntok += 1

    def put_bits(self, pos: int, v: int, w: int) -> None:
        for i in range(w):                  # bit by bit: a token may span two words
            if v >> i & 1:
                q = pos + i
                self.words[q >> 5] |= 1 << (q & 31)

    def value(self) -> int:
        """The stream as songseq.py's number (key + tokens, no part markers)."""
        return to_value(self.words, self.parts)


# ------------------------------------------------------------------ unpacker

def get_bits(words: list, pos: int, w: int) -> int:
    v = 0
    for i in range(w):
        q = pos + i
        v |= (words[q >> 5] >> (q & 31) & 1) << i
    return v


def to_value(words: list, parts: int) -> int:
    """words[] (parts 128 bits apart) -> key + tokens as songseq.py's number."""
    v = get_bits(words, 0, KEY_BITS)
    a = KEY_BITS
    for i in range(NTOK * parts):
        v |= get_bits(words, a, 3) << (KEY_BITS + 3 * i)
        a = next_addr(a)
    return v


def from_value(v: int, parts: int) -> list:
    """songseq.py's number -> words[] (part marker bits left 0)."""
    w = [0] * NWORDS
    w[0] = v & 15
    a = KEY_BITS
    for i in range(NTOK * parts):
        t = v >> (KEY_BITS + 3 * i) & 7
        for b in range(3):
            if t >> b & 1:
                w[(a + b) >> 5] |= 1 << ((a + b) & 31)
        a = next_addr(a)
    return w


END_DONE, END_OUT, END_CUT = 0, 1, 2  # unpack_end(): how the stream ended
last_set = 0                          # unpack_end(): the stream's settings code


def unpack(words: list, parts: int = 1) -> list:
    return unpack_end(words, parts)[0]


def unpack_end(words: list, parts: int = 1) -> tuple:
    """Floppy words (4 x 32 bits per part, 106 used) -> (bar bytes, end).
    Mirrors songseq.unpack() in copy mode with SECT, junk included. end:
    END_DONE = END, zero tail or 128 bars; END_OUT = the tokens of all parts
    ran out between two items (the song may go on in a missing part);
    END_CUT = ran out inside an event (it surely goes on). Settings code ->
    last_set."""
    global last_set
    out = [0] * MAX_BARS
    rh = [0] * MAX_BARS
    key = get_bits(words, 0, KEY_BITS)
    if key > 11:
        key = 0
    ntok = NTOK * parts
    n = 0
    pc = 0
    a = KEY_BITS
    beat = flat = sw = pend = 0
    fin = END_OUT

    def append(b: int) -> None:
        nonlocal n
        out[n] = b
        if (b & 31) == B_REST:
            rh[n] = rh[n - 1] if n > 0 else 0
        else:
            rh[n] = b >> 5
        n += 1

    def tok() -> int:
        nonlocal pc, a
        t = get_bits(words, a, 3)
        pc += 1
        a = next_addr(a)
        return t

    st = tok()                              # settings: one token, or 7 + one more
    if st == SET_ESC:
        e = tok()
        st = SET_ESC + e if e < SET_N - SET_ESC else 0
    while n < MAX_BARS and pc < ntok:
        t = tok()
        if t != EV:
            if flat and t == REST_TOK:
                append(B_REST)
            else:
                d = t - 1
                root = mod12(key + DEG_OFS[d] - flat)
                minor = (0 if flat else DEG_MIN[d]) ^ sw
                append((root << 1 | minor) | beat << 5)
            flat = sw = pend = 0
            continue
        if pc >= ntok:
            break
        e = tok()
        if e == E_END:
            fin = END_DONE
            break
        pend = 1                            # an event: until the next bar or block
        if e == E_SECT:
            if pc >= ntok:
                fin = END_CUT
                break
            r = tok()
            end = 0                         # last section of rhythm r in out[:n]
            i = n - 1
            while i >= 0 and rh[i] != r:
                i -= 1
            if i >= 0:
                end = i + 1
                while i >= 0 and rh[i] == r:
                    i -= 1
                s = i + 1
                for q in range(s, end):
                    if n >= MAX_BARS:
                        break
                    append(out[q])
                beat = rh[n - 1]
            pend = 0
        elif e == E_SWITCH:
            sw ^= 1
        elif e == E_FLAT:
            flat = 1
        elif e == E_NEXT:
            beat = beat + 1 if beat < 2 else 0
        elif e == E_PREV:
            beat = beat - 1 if beat > 0 else 2
        elif e == E_COPY:
            if pc + 3 > ntok:
                fin = END_CUT
                break
            N = (tok() | tok() << 3) + 1
            c = tok()
            if N <= n:
                total = N
                for _ in range(c):
                    total += N
                for _ in range(total):
                    if n >= MAX_BARS:
                        break
                    append(out[n - N])
                beat = rh[n - 1]
            pend = 0
        elif e == E_REP3:
            if n > 0:
                for _ in range(3):
                    if n >= MAX_BARS:
                        break
                    append(out[n - 1])
            pend = 0
    if n >= MAX_BARS:
        fin = END_DONE
    elif fin == END_OUT and pend:
        fin = END_CUT                       # ran out after FLAT / SWITCH / NEXT / PREV
    last_set = st
    return out[:n], fin


# ------------------------------------------------------------------ tests

def to_byte(b: tuple) -> int:
    if b == ref.REST:
        return B_REST
    return (b[0] << 1 | b[1]) | b[2] << 5


def to_words(v: int) -> list:
    return [(v >> (32 * i)) & 0xFFFFFFFF for i in range(4)]


def check(bars_t: list, pk: Packer, stats: dict, parts: int = 1, settings: int = 0) -> None:
    bars = [to_byte(b) for b in bars_t]
    pk.run(bars, parts, settings)
    with ref.parts(parts):
        r = ref.pack(bars_t, settings=settings)
        rb = None if r is None else r[2]
        mine = pk.bits if pk.fits else None
        if mine is not None:
            got, end = unpack_end(pk.words, parts)
            assert got == bars and end != END_CUT, (bars, got, end)
            assert last_set == settings, (last_set, settings)
            assert [to_byte(b) for b in ref.unpack(pk.value())] == bars
            assert ref.last_set == settings, (ref.last_set, settings)
            if parts > 1 and mine > KEY_BITS + BUDGET * (parts - 1):
                # one Floppy fewer: a prefix, never "done"
                got, end = unpack_end(pk.words, parts - 1)
                assert got == bars[:len(got)] and end != END_DONE, (bars, got, end)
                stats["short"] += 1
    if rb is None:
        assert mine is None or mine <= KEY_BITS + BUDGET * parts
        stats["fit_new"] += mine is not None
    else:
        assert mine is not None and mine <= rb, (bars_t, mine, rb)
        stats["cheaper"] += mine < rb
        stats["saved"] += rb - mine
    stats["songs"] += 1
    stats["ops_max"] = max(stats["ops_max"], pk.ops)
    stats["row_max"] = max(stats["row_max"], pk.row_max)
    stats["steps_max"] = max(stats["steps_max"], pk.steps)


def named_songs() -> dict:
    c = ref.chords
    verse = c("Am F C G") * 2
    chorus = c("F G C Am", 1) * 2
    bridge = c("Dm Dm F G", 2)
    return {
        "I-V-vi-IV in G, 1 bar each, x4": c("G D Em C") * 4,
        "I-V-vi-IV in G, 4 bars each": [b for b in c("G D Em C") for _ in range(4)],
        "D C G (I-bVII-IV), 2 bars each, x4": [b for b in c("D C G G") for _ in range(2)] * 4,
        "12-bar blues in A, x2": c("A D A A D D A A E D A E") * 2,
        "Am F C G + E (V of Am), x4": c("Am F C E") * 4,
        "verse x2 + chorus x2 (rhythm 2), x2": (verse + chorus) * 2,
        "V C V C bridge C C": (verse + chorus) * 2 + bridge + chorus * 2,
        "verse/chorus, 4 bars per chord": [b for b in (verse[:4] + chorus[:4]) * 2 for _ in range(4)],
        "one chord, 128 bars": c("E") * 128,
        "empty": [],
        "Am F G x3, Am x4, rest, Am F Dm G x2; x2": user_song(0),
        "the same, second part rhythm 2": user_song(1),
    }


def user_song(r2: int) -> list:
    """(Am Am F G x3, Am x4, rest, (Am Am F F Dm Dm G G) x2) x2 (an example song)."""
    c = ref.chords
    a = c("Am Am F G") * 3 + c("Am Am Am Am")
    b = c("Am Am F F Dm Dm G G", r2) * 2
    return (a + [ref.REST] + b) * 2


def main() -> None:
    quick = "--quick" in sys.argv
    pk = Packer()
    assert unpack([0, 0, 0, 0]) == []
    assert unpack_end([0] * NWORDS, MAX_PARTS) == ([], END_DONE)
    stats = dict(songs=0, cheaper=0, saved=0, fit_new=0, ops_max=0, row_max=0, steps_max=0,
                 short=0)

    lines = ["# songpack test vectors: bars (hex bytes: chord | rhythm << 5, 24 = rest)",
             "# pack: P <bits or FULL> <key> <floppy value, 106 bits, hex> <bars>",
             "# pack, k Floppies: Pk <bits or FULL> <key> <stream value: key + 34 k tokens, hex> <bars>",
             "# unpack: U <floppy value hex> <bars>   (any value, junk included)",
             "# unpack, k Floppies: Uk <end 0 done / 1 ran out / 2 cut> <stream value hex> <bars>"]
    print(f"{'song':40} bars   ref  this")
    for name, bars_t in named_songs().items():
        check(bars_t, pk, stats)
        r = ref.pack(bars_t)
        print(f"{name:40} {len(bars_t):4}  {r[2] if r else 'FULL':>4}  "
              f"{pk.bits if pk.fits else 'FULL':>4}")

    rng = random.Random(7)
    songs = []
    for i in range(60 if quick else 300):
        songs.append(ref.random_song(rng))
        n = rng.randrange(1, 40)
        songs.append([(rng.randrange(12), rng.randrange(2), rng.randrange(3))
                      if rng.random() > 0.1 else ref.REST for _ in range(n)])
    for s in songs:
        check(s, pk, stats)
    for i, s in enumerate(songs[:100] + list(named_songs().values())):   # other settings codes
        check(s, pk, stats, 1 + i % MAX_PARTS, rng.randrange(SET_N))

    for bars_t in list(named_songs().values()) + songs[:80]:
        bars = [to_byte(b) for b in bars_t]
        pk.run(bars)
        hx = "".join(f"{b:02x}" for b in bars) or "-"
        if pk.fits:
            lines.append(f"P {pk.bits} {pk.bkey} {pk.value():027x} {hx}")
        else:
            lines.append(f"P FULL - - {hx}")

    # more Floppies: longer songs, and every song with one Floppy fewer
    long_songs = []
    for i in range(20 if quick else 100):
        a = ref.random_song(rng)
        long_songs.append((a + ref.random_song(rng) + a)[:MAX_BARS])
        long_songs.append([(rng.randrange(12), rng.randrange(2), rng.randrange(3))
                           if rng.random() > 0.1 else ref.REST for _ in range(rng.randrange(20, 129))])
    fit = [0] * (MAX_PARTS + 1)
    for s in long_songs + list(named_songs().values()):
        for k in range(2, MAX_PARTS + 1):
            check(s, pk, stats, k)
            fit[k] += pk.fits
    for k in range(2, MAX_PARTS + 1):
        for bars_t in long_songs[:20]:
            bars = [to_byte(b) for b in bars_t]
            pk.run(bars, k)
            hx = "".join(f"{b:02x}" for b in bars) or "-"
            if pk.fits:
                lines.append(f"P{k} {pk.bits} {pk.bkey} {pk.value():x} {hx}")
            else:
                lines.append(f"P{k} FULL - - {hx}")

    # unpacker: random values (junk) must decode exactly like the reference
    for i in range(2000 if quick else 20000):
        v = rng.getrandbits(CAP_BITS)
        if i % 3 == 0:                      # mostly events, to hit COPY / SECT / REP3
            v = rng.getrandbits(KEY_BITS)
            for t in range(NTOK):
                tk = 0 if rng.random() < 0.5 else rng.randrange(8)
                v |= tk << (KEY_BITS + 3 * t)
        got = unpack(to_words(v))
        want = [to_byte(b) for b in ref.unpack(v)]
        assert got == want, (hex(v), got, want)
        if i < 200:
            lines.append(f"U {v:027x} {''.join(f'{b:02x}' for b in got) or '-'}")
    for i in range(2000 if quick else 20000):  # more Floppies, markers in the extra ones
        k = 2 + i % (MAX_PARTS - 1)
        w = [0] * NWORDS
        for q in range(k):
            for b in range(4):
                w[4 * q + b] = rng.getrandbits(32 if b < 3 else 10)
            if q > 0:
                w[4 * q] = w[4 * q] & ~15 | (11 + q)
        if i % 3 == 0:                      # mostly events
            v = rng.getrandbits(KEY_BITS)
            for t in range(NTOK * k):
                tk = 0 if rng.random() < 0.5 else rng.randrange(8)
                v |= tk << (KEY_BITS + 3 * t)
            w = from_value(v, k)
        v = to_value(w, k)
        got, end = unpack_end(w, k)
        with ref.parts(k):
            want = [to_byte(b) for b in ref.unpack(v)]
        assert got == want, (hex(v), k, got, want)
        if i < 60:
            lines.append(f"U{k} {end} {v:x} {''.join(f'{b:02x}' for b in got) or '-'}")

    out = HERE / "songpack_vectors.txt"
    out.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))
    print(f"\n{stats['songs']} songs: all round trips ok, never dearer than the reference; "
          f"{stats['cheaper']} cheaper ({stats['saved']} bits saved), "
          f"{stats['fit_new']} fit only here")
    print("more Floppies: " + ", ".join(f"{k}: {fit[k]} of {len(long_songs) + len(named_songs())} fit"
                                         for k in range(2, MAX_PARTS + 1))
          + f"; {stats['short']} read back with one Floppy fewer as a prefix, never done")
    print(f"unpacker matches the reference on {2000 if quick else 20000} random values "
          f"for 1 Floppy and as many for 2..{MAX_PARTS}")
    print(f"work per pack (inner-loop ops): max {stats['ops_max']}, max per step "
          f"{stats['row_max']}, max steps {stats['steps_max']}")
    print(f"vectors -> {out.name}")


if __name__ == "__main__":
    sys.exit(main())
