"""Storage model for the Melody Box steps (grids -> token stream -> storage words).

Two tracks; the editor shows n grid slots per track (n = 16, 32, 64 or 128,
the same for both): rest, hold (the note before goes on) or a note (pitch
0..36, three octaves + 1). Stored is one 3-bit token stream: track 1's
events, then track 2's (note or rest + length in slots), pitch as
scale-degree steps in a key the packer picks per track (12 tries), so holds
cost nothing and small steps cost 3 bits.

Storage parts, part q in words[4 q .. 4 q + 3] (bits 128 q ..): part 0 =
Melody Box's own storage knobs 4-9 (8+8+8+8+27+8 = 67 bits: track 1's key in
bits 0..3, then 21 tokens); parts 1.. = the storage effects after it, 106
bits each (Floppy's patch field widths). With SndBank in the chain (sb = 1)
it is part 1: bits 0..3 track 1's sound, 4..7 track 2's sound (SndBank's
sound numbers; not part of the stream), then 32 tokens (bits 8..103), bits
104..105 unused. The others (Floppies) in SongBox's Floppy layout
(songpack.py): the first 4 bits the part marker, then 34 tokens. At most 5
parts (Melody Box + 4 effects fill the 5 slots). One chain holds both
tracks.

Stream: settings token first (bits 0..1: n = 16 << code; bit 2: step length,
0 = 1/16, 1 = 1/8; only playback uses it; both tracks), track 1's events,
then track 2: its key in 2 tokens (k0 | (k1 & 1) << 3; above 11 reads as 0),
its events. Track 2 starts where track 1 ended (END, slot n or the tokens
ran out); fewer than 2 tokens left = track 2 empty. An empty track 2 is
not written, and the last written track's END is left out (zero tokens read
as ESC END, and running out of tokens ends a track too).
Per track the state starts fresh: degree cur = 7 (the key's tonic in the middle
octave), length L = 2 slots, slot position p. D = distance tokens: 2 for
n <= 64 (6 bits), 3 for n = 128; distance = value + 1 (copies and rhythms
read the own track only).
  1..7          note at degree cur + (t - 4), L slots (4 = the same note)
  0 = ESC, then:
    0 END       the rest of the n slots: rests (zero bits = 16 empty steps)
    1 REST      L slots of rest
    2 x         LEN: L = LEN_T[x] (1 2 3 4 6 8 12 16 slots), kept until the next
    3 D x       RHYTHM: the next CP_T[x] slots take rest / hold / note from the
                slot `distance` before; each note slot reads one note (main
                token, after optional SHARP / JUMP), 1 slot long (the holds
                come from the rhythm). Any other ESC code ends it.
    4 SHARP     the next note 1 semitone up (one-shot)
    5 x         JUMP: cur += x - 7 (x 0..3) or x (x 4..7); no note
    6 D x s     COPY: CP_T[x] slots from `distance` back, notes moved by s - 3
                scale degrees (s 0..6; 7 reads as 0)
    7 x         EXTEND: LEN_T[x] more slots of the event before (hold / rest)
Copies and rhythms may overlap their source and may run past slot n (cut).
Junk decodes too: pitches fold back by octaves, cur is clamped, a copy or
rhythm reaching before slot 0 is skipped, a hold after a rest or at slot 0
reads as a rest.

python -B melopack.py           size table, round trips, junk check, vectors
python -B melopack.py --quick   fewer random melodies
"""
from __future__ import annotations

import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

MAXN = 128                    # most steps
NPITCH = 37                   # pitch 0..36
REST, HOLD = 0, 1             # grid values; a note = 2 + pitch
KEY_BITS = 4
NTOK = 34                     # 3-bit tokens per storage effect (parts 1..)
PART_BITS = 3 * NTOK          # 102 token bits per storage effect
OWN_TOK = 21                  # tokens in part 0 (Melody Box's knobs 4-9)
OWN_END = KEY_BITS + 3 * OWN_TOK    # 67: part 0's end bit
SB_SKIP = 8                   # SndBank's part: the two sounds' bits before its tokens
SB_TOK = 32                   # tokens in SndBank's part
SB_END = 128 + SB_SKIP + 3 * SB_TOK  # 232: SndBank part's end bit
MAX_PARTS = 5
NWORDS = 4 * MAX_PARTS
INF = 1 << 30
last_step = 0                 # unpack(): the stream's step length flag

LEN_T = (1, 2, 3, 4, 6, 8, 12, 16)          # LEN / EXTEND, slots
CP_T = (4, 6, 8, 12, 16, 24, 32, 48)        # COPY / RHYTHM length, slots
L_DEFAULT = 1                 # LEN_T index: 2 slots
START_DEG = 7                 # tonic of the middle octave
CUR_MIN, CUR_MAX = -14, 28    # decoder clamp (valid streams stay in -10..24)

T_ESC = 0
E_END, E_REST, E_LEN, E_RHY, E_SHARP, E_JUMP, E_COPY, E_EXT = range(8)
C_SET, C_ESC, C_LEN, C_JUMP, C_EXT = 3, 6, 9, 9, 9  # bits
C_KEY2 = 6                    # track 2's key: 2 tokens

DEG_OFS = (0, 2, 4, 5, 7, 9, 11)          # major scale
# semitone above the key (0..11) -> degree | sharp << 3
PC_DEG = (0, 0 | 8, 1, 1 | 8, 2, 3, 3 | 8, 4, 4 | 8, 5, 5 | 8, 6)


def dist_tokens(n: int) -> int:
    return 2 if n <= 64 else 3


def copy_bits(n: int) -> int:
    return 3 * (4 + dist_tokens(n))


def rhy_bits(n: int) -> int:
    return 3 * (3 + dist_tokens(n))


# ------------------------------------------------------------------ pitch

def deg_semis(d: int) -> int:
    """Degree (any integer) -> semitones above the key's tonic of octave 0."""
    o = 0
    while d < 0:
        d += 7
        o -= 12
    while d >= 7:
        d -= 7
        o += 12
    return o + DEG_OFS[d]


def pitch_deg(p: int, key: int) -> tuple:
    """Pitch -> (degree, sharp) in the key (every semitone outside the major
    scale is a scale degree + 1)."""
    r = p - key
    o = 0
    while r < 0:
        r += 12
        o -= 7
    while r >= 12:
        r -= 12
        o += 7
    c = PC_DEG[r]
    return o + (c & 7), c >> 3


def shift_pitch(p: int, key: int, s: int) -> int:
    """Pitch moved by s scale degrees (sharp kept); may leave 0..36."""
    d, x = pitch_deg(p, key)
    return key + deg_semis(d + s) + x


def normalise(g: list) -> list:
    """Hold after a rest or at slot 0 -> rest; junk values -> rest."""
    out = [REST] * len(g)
    prev = REST
    for i in range(len(g)):
        v = g[i]
        if v > NPITCH + 1 or (v == HOLD and prev == REST):
            v = REST
        out[i] = v
        prev = v
    return out


# ------------------------------------------------------------------ bits

def next_addr(a: int, sb: int = 0) -> int:
    """Bit address of the token after the one at a (parts 128 bits apart,
    tokens in bits 4..66 of part 0, bits 8..103 of SndBank's part 1 (sb),
    bits 4..105 of the others). A token never ends at bit 232 except in
    SndBank's part, so only part 1's start needs sb."""
    a += 3
    if a == OWN_END:
        a = 128 + (SB_SKIP if sb else KEY_BITS)
    elif (a & 127) == 106 or a == SB_END:
        a = (a | 127) + 1 + KEY_BITS
    return a


def ntok(parts: int, sb: int = 0) -> int:
    """Tokens in parts 0 .. parts - 1 (sb: part 1 is SndBank's)."""
    return OWN_TOK + NTOK * (parts - 1) - (NTOK - SB_TOK if sb and parts > 1 else 0)


def need_parts(bits: int, sb: int = 0) -> int:
    """Parts for `bits` token bits (99: more than MAX_PARTS)."""
    for k in range(1, MAX_PARTS + 1):
        if bits <= 3 * ntok(k, sb):
            return k
    return 99


def put_bits(words: list, pos: int, v: int, w: int) -> None:
    for i in range(w):
        if v >> i & 1:
            q = pos + i
            words[q >> 5] |= 1 << (q & 31)


def get_bits(words: list, pos: int, w: int) -> int:
    v = 0
    for i in range(w):
        q = pos + i
        v |= (words[q >> 5] >> (q & 31) & 1) << i
    return v


# ------------------------------------------------------------------ unpacker

def unpack(words: list, parts: int = 1, sb: int = 0) -> tuple:
    """Storage words (4 per part; sb: part 1 is SndBank's) -> (track 1 grid,
    track 2 grid), n slots each (normalised); the step length flag (0 =
    1/16, 1 = 1/8) -> last_step."""
    global last_step
    key = get_bits(words, 0, KEY_BITS)
    if key > 11:
        key = 0
    nt = ntok(parts, sb)
    st = [KEY_BITS, 0]                      # bit address, tokens read

    def tok() -> int:
        t = get_bits(words, st[0], 3)
        st[0] = next_addr(st[0], sb)
        st[1] += 1
        return t

    x = tok()
    n = 16 << (x & 3)
    last_step = x >> 2 & 1
    g1 = track(tok, st, nt, n, key)
    g2 = [REST] * n
    if st[1] + 2 <= nt:                     # track 2: key, events
        key = tok()
        key |= (tok() & 1) << 3
        if key > 11:
            key = 0
        g2 = track(tok, st, nt, n, key)
    return g1, g2


def track(tok, st: list, ntok: int, n: int, key: int) -> list:
    """One track's events from the token reader -> grid of n slots."""
    g = [REST] * MAXN

    def last_deg(p: int) -> int:
        q = p - 1
        while q >= 0 and g[q] < 2:
            q -= 1
        return pitch_deg(g[q] - 2, key)[0] if q >= 0 else START_DEG

    dt = dist_tokens(n)
    p = 0
    cur = START_DEG
    L = LEN_T[L_DEFAULT]
    sharp = 0
    rleft = rdist = 0                       # RHYTHM: slots left, distance
    while p < n:
        if rleft:
            v = g[p - rdist]
            if v < 2:                       # rest / hold from the rhythm
                g[p] = HOLD if v == HOLD and g[p - 1] != REST else REST
                p += 1
                rleft -= 1
                continue
        if st[1] >= ntok:
            break
        t = tok()
        if t != T_ESC:
            d = cur + t - 4
            pitch = key + deg_semis(d) + sharp
            while pitch >= NPITCH:
                pitch -= 12
                d -= 7
            while pitch < 0:
                pitch += 12
                d += 7
            cur = d
            sharp = 0
            g[p] = 2 + pitch
            p += 1
            if rleft:
                rleft -= 1
                continue
            i = 1
            while i < L and p < n:
                g[p] = HOLD
                p += 1
                i += 1
            continue
        if st[1] >= ntok:
            break
        e = tok()
        if e == E_SHARP:
            sharp = 1
            continue
        if e != E_JUMP:
            rleft = 0                       # any other command ends a rhythm
        if e == E_END:
            break
        if e == E_REST:
            i = 0
            while i < L and p < n:
                g[p] = REST
                p += 1
                i += 1
        elif e == E_RHY or e == E_COPY:
            if st[1] + dt + (2 if e == E_COPY else 1) > ntok:
                break
            dist = 0
            for i in range(dt):
                dist |= tok() << (3 * i)
            dist += 1
            ln = CP_T[tok()]
            s = tok() - 3 if e == E_COPY else 0
            if s == 4:
                s = 0
            if dist > p:
                continue
            if e == E_RHY:
                rleft, rdist = ln, dist
                continue
            i = 0
            while i < ln and p < n:
                v = g[p - dist]
                if v >= 2 and s:
                    pitch = shift_pitch(v - 2, key, s)
                    while pitch >= NPITCH:
                        pitch -= 12
                    while pitch < 0:
                        pitch += 12
                    v = 2 + pitch
                elif v == HOLD and g[p - 1] == REST:
                    v = REST
                g[p] = v
                p += 1
                i += 1
            cur = last_deg(p)
        else:                               # LEN, JUMP, EXTEND: one more token
            if st[1] >= ntok:
                break
            x = tok()
            if e == E_LEN:
                L = LEN_T[x]
            elif e == E_JUMP:
                cur += x - 7 if x < 4 else x
                if cur < CUR_MIN:
                    cur = CUR_MIN
                if cur > CUR_MAX:
                    cur = CUR_MAX
            else:                           # EXTEND
                v = HOLD if p > 0 and g[p - 1] != REST else REST
                i = 0
                while i < LEN_T[x] and p < n:
                    g[p] = v
                    p += 1
                    i += 1
    return g[:n]


# ------------------------------------------------------------------ packer

def interval_bits(i: int) -> int:
    """Bits of a degree step: one note token, after a JUMP per 7 beyond +-3."""
    b = 3
    while i > 3:
        i -= 7 if i > 7 else i
        b += C_JUMP
    while i < -3:
        i -= -7 if i < -7 else i
        b += C_JUMP
    return b


class Tables:
    """Per grid and key: note bits, slot runs, best COPY / RHYTHM sources."""

    def __init__(self, g: list, key: int):
        n = len(g)
        self.g, self.n, self.key = g, n, key
        n1 = n + 1
        self.nc = [0] * n1                  # note bits at p (0 elsewhere)
        cur = START_DEG
        for p in range(n):
            if g[p] >= 2:
                d, s = pitch_deg(g[p] - 2, key)
                self.nc[p] = interval_bits(d - cur) + (C_ESC if s else 0)
                cur = d
        self.pre = [0] * n1                 # prefix sums of nc
        for p in range(n):
            self.pre[p + 1] = self.pre[p] + self.nc[p]
        self.note_len = [0] * n1
        self.hold_run = [0] * n1
        self.rest_run = [0] * n1
        for p in range(n - 1, -1, -1):
            if g[p] == HOLD:
                self.hold_run[p] = self.hold_run[p + 1] + 1
            if g[p] == REST:
                self.rest_run[p] = self.rest_run[p + 1] + 1
            if g[p] >= 2:
                self.note_len[p] = 1 + self.hold_run[p + 1]
        # longest COPY (any shift) and RHYTHM run from p, and its source
        self.cmax = [0] * n1
        self.csrc = [(0, 0)] * n1
        self.rmax = [0] * n1
        self.rsrc = [0] * n1
        for s in (0, -1, 1, -2, 2, -3, 3):
            sh = [v for v in range(NPITCH + 2)]
            if s:
                for v in range(2, NPITCH + 2):
                    q = shift_pitch(v - 2, key, s)
                    sh[v] = 2 + q if 0 <= q < NPITCH else -1
            self._runs(lambda a, b: a == sh[b], s)
        self._runs(lambda a, b: (a >= 2) == (b >= 2) and (a >= 2 or a == b), None)

    def _runs(self, eq, s) -> None:
        g, n = self.g, self.n
        row = [0] * (n + 1)
        for p in range(n - 1, 0, -1):
            new = [0] * (n + 1)
            for d in range(1, p + 1):
                if eq(g[p], g[p - d]):
                    r = row[d] + 1
                    new[d] = r
                    if s is None:
                        if r > self.rmax[p]:
                            self.rmax[p], self.rsrc[p] = r, d
                    elif r > self.cmax[p]:
                        self.cmax[p], self.csrc[p] = r, (d, s)
            row = new


def dp(t: Tables, path: bool = False, c0: int = C_SET, endc: int = C_ESC):
    """Cheapest token bits (c0: the track's first token(s): track 1's
    settings, track 2's key; endc: END's cost, 0 for the last track written);
    with path the moves."""
    g, n = t.g, t.n
    cb, rb = copy_bits(n), rhy_bits(n)
    cost = [[INF] * 8 for _ in range(n + 1)]
    frm = [[None] * 8 for _ in range(n + 1)]
    cost[0][L_DEFAULT] = c0
    best, bstate = INF, None

    def relax(q: int, lj: int, c: int, mv: tuple) -> None:
        if c < cost[q][lj]:
            cost[q][lj] = c
            frm[q][lj] = mv

    for p in range(n + 1):
        for li in range(8):
            c = cost[p][li]
            if c >= INF:
                continue
            if p == n or t.rest_run[p] == n - p:
                f = c + (endc if p < n else 0)
                if f < best:
                    best, bstate = f, (p, li)
                if p == n:
                    continue
            v = g[p]
            if v >= 2:
                for lj in range(8):
                    if LEN_T[lj] <= t.note_len[p]:
                        relax(p + LEN_T[lj], lj, c + t.nc[p] + (C_LEN if lj != li else 0),
                              ("N", p, li))
            elif v == HOLD:
                for x in range(8):
                    if LEN_T[x] <= t.hold_run[p]:
                        relax(p + LEN_T[x], li, c + C_EXT, ("X", p, li, x))
            else:
                r = t.rest_run[p]
                for lj in range(8):
                    if LEN_T[lj] <= r:
                        relax(p + LEN_T[lj], lj, c + C_ESC + (C_LEN if lj != li else 0),
                              ("R", p, li))
                if p > 0 and g[p - 1] == REST:
                    for x in range(8):
                        if LEN_T[x] <= r:
                            relax(p + LEN_T[x], li, c + C_EXT, ("X", p, li, x))
            for x in range(8):
                ln = CP_T[x]
                q = p + ln if p + ln < n else n
                if ln <= t.cmax[p] or (t.cmax[p] and t.cmax[p] == n - p):
                    relax(q, li, c + cb, ("C", p, li, x))
                if ln <= t.rmax[p] or (t.rmax[p] and t.rmax[p] == n - p):
                    relax(q, li, c + rb + t.pre[q] - t.pre[p], ("Y", p, li, x))
    if not path:
        return best
    moves = []
    q, lj = bstate
    while frm[q][lj] is not None:
        mv = frm[q][lj]
        moves.append((mv, q, lj))
        q, lj = mv[1], mv[2]
    moves.reverse()
    return best, moves, bstate[0]


def note_tokens(toks: list, d: int, s: int, cur: int) -> None:
    i = d - cur
    while i > 3:
        j = 7 if i > 7 else i
        toks += [T_ESC, E_JUMP, j]
        i -= j
    while i < -3:
        j = -7 if i < -7 else i
        toks += [T_ESC, E_JUMP, j + 7]
        i -= j
    if s:
        toks += [T_ESC, E_SHARP]
    toks.append(i + 4)


def emit(t: Tables, step8: int = 0, trk: int = 0, endc: int = C_ESC) -> tuple:
    """DP path -> (token list, token bits). trk 0: the settings token first,
    1: the key's 2 tokens; endc 0: no END (the last track written)."""
    c0 = C_KEY2 if trk else C_SET
    best, moves, endp = dp(t, path=True, c0=c0, endc=endc)
    g, n, key = t.g, t.n, t.key
    dt = dist_tokens(n)
    if trk:
        toks = [key & 7, key >> 3]
    else:
        toks = [{16: 0, 32: 1, 64: 2, 128: 3}[n] | step8 << 2]
    cur = START_DEG

    def dist(d: int) -> list:
        d -= 1
        return [(d >> (3 * i)) & 7 for i in range(dt)]

    for mv, q, lj in moves:
        k, p, li = mv[0], mv[1], mv[2]
        if k in "NR" and lj != li:
            toks += [T_ESC, E_LEN, lj]
        if k == "N":
            d, s = pitch_deg(g[p] - 2, key)
            note_tokens(toks, d, s, cur)
            cur = d
        elif k == "R":
            toks += [T_ESC, E_REST]
        elif k == "X":
            toks += [T_ESC, E_EXT, mv[3]]
        elif k == "C":
            d, s = t.csrc[p]
            toks += [T_ESC, E_COPY] + dist(d) + [mv[3], s + 3]
            r = q - 1
            while r >= 0 and g[r] < 2:
                r -= 1
            cur = pitch_deg(g[r] - 2, key)[0] if r >= 0 else START_DEG
        else:
            toks += [T_ESC, E_RHY] + dist(t.rsrc[p]) + [mv[3]]
            for r in range(p, q):
                if g[r] >= 2:
                    d, s = pitch_deg(g[r] - 2, key)
                    note_tokens(toks, d, s, cur)
                    cur = d
    if endp < n and endc:
        toks += [T_ESC, E_END]
    assert 3 * len(toks) == best, (3 * len(toks), best)
    return toks, best


def best_key(g: list, c0: int, endc: int) -> Tables:
    """The key with the cheapest DP (the first of equal ones)."""
    bt, bc = None, INF
    for k in range(12):
        t = Tables(g, k)
        c = dp(t, c0=c0, endc=endc)
        if c < bc:
            bt, bc = t, c
    return bt


def pack(g: list, step8: int = 0, g2: list = None, sb: int = 0) -> tuple:
    """Track grids (16 / 32 / 64 / 128 slots, both the same; g2 None = empty)
    + step length flag (1 = 1/8), sb (part 1 is SndBank's; its sound bits
    left 0) -> (words, key 1, token bits, parts needed or 0 = doesn't fit
    MAX_PARTS, key 2 (0 if track 2 is empty))."""
    g = normalise(g)
    assert len(g) in (16, 32, 64, 128), len(g)
    g2 = normalise(g2) if g2 is not None else [REST] * len(g)
    assert len(g2) == len(g)
    has2 = any(v != REST for v in g2)
    end1 = C_ESC if has2 else 0
    bt = best_key(g, C_SET, end1)
    toks, bits = emit(bt, step8, 0, end1)
    key2 = 0
    if has2:
        bt2 = best_key(g2, C_KEY2, 0)
        t2, b2 = emit(bt2, 0, 1, 0)
        toks += t2
        bits += b2
        key2 = bt2.key
    parts = need_parts(bits, sb)
    if parts > MAX_PARTS:
        parts = 0
    words = [0] * NWORDS
    if parts:
        put_bits(words, 0, bt.key, KEY_BITS)
        a = KEY_BITS
        for x in toks:
            put_bits(words, a, x, 3)
            a = next_addr(a, sb)
    return words, bt.key, bits, parts, key2


# ------------------------------------------------------------------ test melodies

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def grid_of(s: str) -> list:
    """'E4:4 r:2 F#3:1 | ...' (C3 = pitch 0, durations in slots) -> grid."""
    g = []
    for w in s.split():
        if w == "|":
            continue
        nm, d = w.split(":")
        d = int(d)
        if nm == "r":
            g += [REST] * d
            continue
        pc = NAMES[nm[0]] + (1 if "#" in nm else 0)
        pitch = (int(nm[-1]) - 3) * 12 + pc
        assert 0 <= pitch < NPITCH, w
        g += [2 + pitch] + [HOLD] * (d - 1)
    assert len(g) in (16, 32, 64, 128), (s[:30], len(g))
    return g


def rep(s: str, n: int) -> str:
    return " ".join([s] * n)


def scaled(s: str, k: int) -> str:
    """Every duration times k."""
    return " ".join(w if w == "|" else f"{w.split(':')[0]}:{k * int(w.split(':')[1])}"
                    for w in s.split())


def scale_run() -> list:
    """64 sixteenths: C major from C3 up two octaves and down, over and over."""
    up = [deg_semis(d) for d in range(15)]
    seq = up + up[-2:0:-1]
    return [2 + seq[i % len(seq)] for i in range(64)]


LESNIK = ("A3:2 C4:2 D4:2 E4:1 A3:1 | r:1 A3:1 C4:2 D4:2 E4:2 | "
          "A3:2 C4:2 G4:2 F4:1 A3:1 | r:1 A3:1 C4:2 G4:2 F4:2 | "
          "G4:2 F4:2 E4:2 D4:1 G4:1 | r:1 G4:1 F4:2 E4:2 D4:2 | "
          "G3:2 B3:2 C4:2 D4:1 G3:1 | r:1 G3:1 B3:2 C4:2 D4:2")    # 8 bars in 8ths


def named() -> dict:
    ode = "E4:2 E4:2 F4:2 G4:2 G4:2 F4:2 E4:2 D4:2 C4:2 C4:2 D4:2 E4:2"
    return {
        "1-bar riff (16)": grid_of("A3:2 C4:1 r:1 E4:2 A3:1 G3:1 r:2 A3:2 C4:2 D4:2"),
        "Ode to Joy, 4 bars, 8ths (32)": grid_of(ode + " E4:3 D4:1 D4:4"),
        "Ode to Joy, 4 bars, quarters": grid_of(
            "E4:4 E4:4 F4:4 G4:4 G4:4 F4:4 E4:4 D4:4 C4:4 C4:4 D4:4 E4:4 E4:6 D4:2 D4:8"),
        "Ode to Joy, 8 bars, eighths": grid_of(
            ode + " E4:3 D4:1 D4:4 " + ode + " D4:3 C4:1 C4:4"),
        "Twinkle, 8 bars, eighths": grid_of(
            "C4:2 C4:2 G4:2 G4:2 A4:2 A4:2 G4:4 F4:2 F4:2 E4:2 E4:2 D4:2 D4:2 C4:4 "
            + rep("G4:2 G4:2 F4:2 F4:2 E4:2 E4:2 D4:4", 2)),
        "Frere Jacques, 8 bars, eighths": grid_of(
            rep("C4:2 D4:2 E4:2 C4:2", 2) + " " + rep("E4:2 F4:2 G4:4", 2) + " "
            + rep("G4:1 A4:1 G4:1 F4:1 E4:2 C4:2", 2) + " " + rep("C4:2 G3:2 C4:4", 2)),
        "Saints, 4 bars": grid_of(rep("r:4 C4:4 E4:4 F4:4 G4:16", 2)),
        "octave bass, eighths, C C F G": grid_of(
            rep("C3:2 C4:2", 8) + " " + rep("F3:2 F4:2", 4) + " " + rep("G3:2 G4:2", 4)),
        "funk bass, 16ths, rests": grid_of(
            rep("E3:1 r:1 E4:1 E3:1 r:1 E3:1 G3:1 A3:1 E3:1 r:1 E4:1 E3:1 r:2 D4:1 D#4:1", 3)
            + " A3:1 r:1 A4:1 A3:1 r:1 A3:1 C4:1 D4:1 E3:4 r:4"),
        "staccato 16ths, note/rest": grid_of(
            rep("C4:1 r:1 E4:1 r:1 G4:1 r:1 E4:1 r:1", 6) + " "
            + rep("D4:1 r:1 F4:1 r:1 A4:1 r:1 F4:1 r:1", 2)),
        "arpeggio 16ths, up/down, Am F C G": grid_of(
            rep("A3:1 C4:1 E4:1 A4:1 C5:1 A4:1 E4:1 C4:1", 2) + " "
            + rep("F3:1 A3:1 C4:1 F4:1 A4:1 F4:1 C4:1 A3:1", 2) + " "
            + rep("C4:1 E4:1 G4:1 C5:1 E5:1 C5:1 G4:1 E4:1", 2) + " "
            + rep("G3:1 B3:1 D4:1 G4:1 B4:1 G4:1 D4:1 B3:1", 2)),
        "blues riff, eighths, A minor pent.": grid_of(
            "A3:2 C4:2 D4:2 D#4:2 E4:2 G4:2 E4:2 D4:2 A3:2 r:2 A3:2 C4:2 A3:4 r:4 "
            "A3:2 C4:2 D4:2 D#4:2 E4:2 G4:2 E4:2 D4:2 E4:2 D4:2 C4:2 A3:2 G3:2 A3:6"),
        "synth-pop, dotted 8ths": grid_of(
            "E4:3 E4:3 D4:2 C4:3 D4:3 E4:2 G4:3 E4:3 D4:2 C4:8 "
            "E4:3 E4:3 D4:2 C4:3 D4:3 E4:2 D4:3 E4:3 D4:2 C4:8"),
        "scale run, 64 16ths": scale_run(),
        "Lesnik bars 17-24, 1/8 grid (64)": grid_of(LESNIK),
        "Lesnik bars 17-24, 1/16 grid (128)": grid_of(scaled(LESNIK, 2)),
        "empty (64)": [REST] * 64,
    }


def random_walk(rng: random.Random, repeat: bool, n: int = 64) -> list:
    """Diatonic random walk, mixed lengths, some rests and sharps; repeat:
    about half the bars after the first copy an earlier bar."""
    key = rng.randrange(12)
    base = rng.choice((1, 2, 2, 4))
    steps = (0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6, 7, -7)
    wts = (20, 20, 20, 10, 10, 4, 4, 3, 3, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5)
    d = START_DEG
    g = []
    while len(g) < n:
        if repeat and len(g) >= 16 and (len(g) & 15) == 0 and rng.random() < 0.5:
            src = 16 * rng.randrange(len(g) >> 4)
            g += g[src:src + 16]
            continue
        dur = base if rng.random() < 0.75 else rng.choice((1, 2, 3, 4, 6, 8))
        if rng.random() < 0.1:
            g += [REST] * dur
            continue
        st = rng.choices(steps, wts)[0]
        d += st
        pitch = key + deg_semis(d)
        if pitch < 0 or pitch >= NPITCH:
            d -= 2 * st
            pitch = key + deg_semis(d)
        if pitch < 0 or pitch >= NPITCH:
            d = START_DEG
            pitch = key + deg_semis(d)
        if rng.random() < 0.05 and pitch + 1 < NPITCH and pitch_deg(pitch + 1, key)[1]:
            pitch += 1
        g += [2 + pitch] + [HOLD] * (dur - 1)
    return normalise(g[:n])


def random_chromatic(rng: random.Random) -> list:
    return [2 + rng.randrange(NPITCH) for _ in range(64)]


def pairs() -> dict:
    """Track 1 + track 2 (lead + bass) test pairs."""
    L = named()
    return {
        "Lesnik + roots, 8ths": (L["Lesnik bars 17-24, 1/8 grid (64)"], grid_of(
            " ".join(rep(f"{r}:1", 8) for r in "A3 A3 F3 F3 G3 D3 G3 G3".split()))),
        "Twinkle + root-fifth, quarters": (L["Twinkle, 8 bars, eighths"], grid_of(
            " ".join(f"{a}:2 {b}:2 {a}:2 {b}:2" for a, b in
                     [("C3", "G3"), ("F3", "C4"), ("C3", "G3"), ("G3", "D4")] * 2))),
        "Ode to Joy + roots, half bars": (L["Ode to Joy, 8 bars, eighths"], grid_of(
            "C3:4 G3:4 C3:4 G3:4 C3:4 G3:4 G3:4 C3:4 C3:4 G3:4 C3:4 G3:4 C3:4 G3:4 G3:4 C3:4")),
        "arpeggio + octave 16ths": (L["arpeggio 16ths, up/down, Am F C G"], grid_of(
            " ".join(rep(f"{r}3:1 {r}4:1", 8) for r in "A F C G".split()))),
        "blues riff + boogie": (L["blues riff, eighths, A minor pent."], grid_of(
            rep("A3:2 A3:2 C#4:2 C#4:2 E4:2 E4:2 F#4:2 E4:2", 4))),
        "synth-pop + funk 16ths": (L["synth-pop, dotted 8ths"], grid_of(
            rep("C3:1 r:1 C4:1 C3:1 r:2 C3:1 G3:1", 4) + " " + rep("A3:1 r:1 A4:1 A3:1 r:2 A3:1 G3:1", 2)
            + " " + rep("F3:1 r:1 F4:1 F3:1 r:2 F3:1 G3:1", 2))),
        "empty + bass only (32)": ([REST] * 32, grid_of(rep("E3:2 r:2 E3:1 E3:1 G3:2", 4))),
        "Ode (128) + roots": (grid_of(scaled(
            "E4:2 E4:2 F4:2 G4:2 G4:2 F4:2 E4:2 D4:2 C4:2 C4:2 D4:2 E4:2 E4:3 D4:1 D4:4 "
            "E4:2 E4:2 F4:2 G4:2 G4:2 F4:2 E4:2 D4:2 C4:2 C4:2 D4:2 E4:2 D4:3 C4:1 C4:4", 2)), grid_of(
            " ".join(f"{r}:8" for r in "C3 G3 C3 G3 C3 G3 G3 C3 C3 G3 C3 G3 C3 G3 G3 C3".split()))),
    }


def root_bass(rng: random.Random, n: int = 64) -> list:
    """Roots of a random I / IV / V / vi progression, one bar each: whole
    notes, 8ths or root-fifth quarters."""
    key = rng.randrange(7)
    style = rng.randrange(3)
    g = []
    while len(g) < n:
        r = 2 + (deg_semis(rng.choice((0, 3, 4, 5))) + key) % 12
        if style == 0:
            g += [r] + [HOLD] * 15
        elif style == 1:
            g += [r, HOLD] * 8
        else:
            g += [r, HOLD, HOLD, HOLD, r + 7, HOLD, HOLD, HOLD] * 2
    return g[:n]


# ------------------------------------------------------------------ report

def check(g: list, step8: int = 0, g2: list = None, sb: int = 0) -> tuple:
    """Pack, read back with as many parts as needed and with MAX_PARTS."""
    g = normalise(g)
    g2 = normalise(g2) if g2 is not None else [REST] * len(g)
    words, key, bits, parts, key2 = pack(g, step8, g2, sb)
    if parts:
        assert unpack(words, parts, sb) == (g, g2) and last_step == step8, (g, g2)
        assert unpack(words, MAX_PARTS, sb) == (g, g2)
        if sb:                              # the sound bits are not read
            w = list(words)
            w[4] |= 0xFF
            assert unpack(w, parts, sb) == (g, g2)
    return words, key, bits, parts, key2


def main() -> None:
    quick = "--quick" in sys.argv
    rng = random.Random(11)
    assert unpack([0] * NWORDS, MAX_PARTS) == ([REST] * 16, [REST] * 16)
    for sb in (0, 1):                       # addresses: every bit of the parts once, in order
        a, seen = KEY_BITS, []
        for _ in range(ntok(MAX_PARTS, sb)):
            seen += range(a, a + 3)
            a = next_addr(a, sb)
        assert seen == sorted(set(seen)) and len(seen) == 3 * ntok(MAX_PARTS, sb)

    lines = ["# melopack test vectors; grid = n hex bytes (0 rest, 1 hold, 2 + pitch); "
             "grid 1 = track 1, grid 2 = track 2",
             "# parts: 1 = Melody Box's knobs 4-9 (21 tokens), then 34 tokens per storage effect; "
             "sb 1: part 2 is SndBank's (32 tokens after 8 sound bits)",
             "# pack: P <token bits> <key 1> <key 2> <step 0 = 1/16, 1 = 1/8> <sb> <parts> "
             "<words, 4 per part, hex> <grid 1> <grid 2>",
             "# unpack: U <sb> <parts> <step> <words, 4 per part, hex> <grid 1> <grid 2>   "
             "(any words, junk incl.)"]

    def gridhex(g: list) -> str:
        return "".join(f"{v:02x}" for v in g)

    def vector(g: list, g2: list, step8: int, sb: int, r: tuple) -> None:
        words, key, bits, parts, key2 = r
        if parts:
            lines.append(f"P {bits} {key} {key2} {step8} {sb} {parts} "
                         + " ".join(f"{w:08x}" for w in words[:4 * parts])
                         + f" {gridhex(normalise(g))} {gridhex(normalise(g2))}")

    print("track 1 alone: token bits (+ 4 key bits); parts: 1 = Melody Box (63 token bits), "
          "2 = + SndBank (159), 3 = + 1 Floppy (261), 4 = + 2 (363), 5 = + 3 (465); "
          "vectors alternate sb 0 / 1\n")
    hdr = f"{'melody':36} {'steps':>5} {'notes':>5} {'P4':>5} {'P6':>5} {'bits':>5}  Pt"
    print(hdr)
    print("-" * len(hdr))
    nv = 0
    for name, g in named().items():
        step8 = 1 if "1/8" in name else 0
        sb = nv & 1
        nv += 1
        r = check(g, step8, None, sb)
        words, key, bits, parts, key2 = r
        ps = [v - 2 for v in g if v >= 2]
        p4 = 4 * len(g) if not ps or max(ps) - min(ps) <= 11 else "-"
        notes = len(ps)
        print(f"{name:36} {len(g):5} {notes:5} {p4:>5} {6 * len(g):5} {bits:5}  "
              f"{parts if parts else '>5':>2}")
        vector(g, [REST] * len(g), step8, sb, r)

    print(f"\n{'track 1 + track 2':36} {'steps':>5} {'alone':>5} {'both':>5}  Pt")
    print("-" * 57)
    for name, (g, g2) in pairs().items():
        step8 = 1 if "1/8" in name else 0
        sb = nv & 1
        nv += 1
        r = check(g, step8, g2, sb)
        words, key, bits, parts, key2 = r
        print(f"{name:36} {len(g):5} {pack(g, step8)[2]:5} {bits:5}  {parts if parts else '>5':>2}")
        vector(g, g2, step8, sb, r)

    nr = 12 if quick else 50
    sets = [("random walk, bars repeat (64)", lambda: (random_walk(rng, True), None)),
            ("random walk, no repeats (64)", lambda: (random_walk(rng, False), None)),
            ("random walk, bars repeat (128)", lambda: (random_walk(rng, True, 128), None)),
            ("random chromatic 16ths (64, worst)", lambda: (random_chromatic(rng), None)),
            ("random walk + root bass (64)", lambda: (random_walk(rng, True), root_bass(rng)))]
    print(f"\n{'set (' + str(nr) + ' each), with SndBank':36} {'mean bits':>9}  parts 1 / 2 / 3 / 4 / 5 / more")
    for name, fn in sets:
        tot = 0
        fit = [0] * 7
        for _ in range(nr):
            g, g2 = fn()
            words, key, bits, parts, key2 = check(g, 0, g2, 1)
            tot += bits
            k = need_parts(bits, 1)
            fit[k if k <= MAX_PARTS else MAX_PARTS + 1] += 1
        print(f"{name:36} {tot // nr:9}  " + " / ".join(str(x) for x in fit[1:]))

    nj = 500 if quick else 5000
    for i in range(nj):                     # junk: any words decode to clean grids
        parts = 1 + i % MAX_PARTS
        sb = i >> 1 & 1
        w = [0] * NWORDS
        for j in range(4 * parts):
            w[j] = rng.getrandbits(32)
        if i % 3 == 0:                      # mostly ESC tokens, to hit COPY / RHYTHM / JUMP
            a = KEY_BITS
            w = [0] * NWORDS
            put_bits(w, 0, rng.randrange(16), KEY_BITS)
            for _ in range(ntok(parts, sb)):
                put_bits(w, a, 0 if rng.random() < 0.5 else rng.randrange(8), 3)
                a = next_addr(a, sb)
        g, g2 = unpack(w, parts, sb)
        assert len(g) in (16, 32, 64, 128) and g == normalise(g), (w, g)
        assert len(g2) == len(g) and g2 == normalise(g2), (w, g2)
        if i < 100:
            lines.append(f"U {sb} {parts} {last_step} " + " ".join(f"{x:08x}" for x in w[:4 * parts])
                         + f" {gridhex(g)} {gridhex(g2)}")

    out = HERE / "melopack_vectors.txt"
    out.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))
    print(f"\nall round trips ok (also read with {MAX_PARTS} parts); {nj} junk streams decode to clean grids")
    print("P4 = plain 4 bit/slot (1 octave only, '-' = wider), P6 = plain 6 bit/slot, "
          "bits = token stream, Pt = parts; alone = track 1 without track 2")
    print(f"vectors -> {out.name}")


if __name__ == "__main__":
    sys.exit(main())
