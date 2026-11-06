"""Song sequencer model: bar list <-> token stream for one Floppy.

The user edits a full bar list (bar = chord root 0..11, minor 0/1, beat 0..2).
Floppy keeps only a packed token stream:

  bits 0..3   key (0..11): chord set I ii iii IV V vi bVII of that major key
              (C: C Dm Em F G Am A#)
  then the settings (SongBox MENU OCTAVE + LENGTH as a code 0..8, 0 = the
  defaults; set_tokens()): code 0..6 = one token, 7 / 8 = token 7 + 0 / 1
  (7 + 2..7 read as 0, free for later)
  then 3-bit tokens, LSB first:
    1..7      a bar with that degree's chord (pending flags applied)
    0         event marker, the next token is the event type:
      0 END        (a zero tail decodes as END, so END costs nothing)
      1 SWITCH     next chord: major <-> minor
      2 FLAT       next chord: root - 1 semitone, default major;
                   FLAT + 4 (degree IV) = rest bar (silent, rhythm unchanged)
                   (FLAT + 7 is spare: it decodes as a chord the packer
                   writes as vi + SWITCH)
      3 NEXT       rhythm 1 -> 2 -> 3 -> 1
      4 PREV       rhythm 3 -> 2 -> 1 -> 3
      5 REP3       last bar 3 more times (chord + rhythm, no events)
      6 COPY       + N (2 tokens, low first: 1..64 bars) + count c: the last N
                   bars play c + 1 more times (copies of copies = nesting)
      7 SECT       + rhythm token r (0..2): replay the last section (run of
                   bars) played with rhythm r; rests count as the rhythm they
                   play in

More Floppies (set_parts / `with parts(k)`, k = 1..4): the stream simply
goes on, 34 more tokens per extra copy (each copy's first 4 bits hold its
part marker, not stream bits; songpack.py lays the parts out).

The packer tries all 12 keys and, per key, finds the cheapest stream with a
dynamic program over bar positions (runs -> REP3, repeated blocks -> COPY,
returning sections -> SECT).

Measured alternatives, kept for comparison (`legacy()` switches SECT off):
`rep_mode="23"` REP2 + REP3 (REP2 on code 7), `rep_mode="n"` one REPN + count
token; `loops=True` loop begin/end (LBEGIN on 6, LEND + count on 4, no
nesting, replays restore the LBEGIN rhythm); FAR_COPY (copy N bars from D
back, 21 bits).
"""
from __future__ import annotations

import random
import sys
from functools import lru_cache

CAP_BITS = 106
KEY_BITS = 4
TOK_BITS = 3
MAX_BARS = 128
NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
DEG_OFS = (0, 2, 4, 5, 7, 9, 10)
DEG_MIN = (0, 1, 1, 0, 0, 1, 0)
EV = 0
E_END, E_SWITCH, E_FLAT, E_NEXT, E_PREV, E_REP3, E_COPY, E_SECT = range(8)
E_REP2 = E_REPN = E_SECT      # legacy rep modes only (SECT off)
E_LBEGIN, E_LEND = E_COPY, E_PREV   # legacy loops=True only
EVB = 2 * TOK_BITS            # one event = marker + type
LOOP_BITS = 2 * EVB + TOK_BITS
BUDGET = CAP_BITS - KEY_BITS  # token bits available
PART_BITS = 102               # token bits per Floppy (106 - 4-bit key or part marker)
COPY_LENS = [1, 2, 3, 4, 6, 8, 12, 16]
SECT_CODE = E_SECT            # None = no SECT (legacy modes)
FAR_COPY = False              # experiment: FLAT + token 7 + D (2 tok) + N (2 tok):
                              # copy N bars starting D bars back (21 bits)
FAR_TOK = 7
COPY_WIDE = True              # True: N = 1..64 in two tokens
PREV_EV = False               # set by pack/unpack for blocks "copy"
LOOP_MAX = 9                  # count token 0..7 -> 2..9 plays

Bar = tuple                   # (root, minor, beat)
REST = (12, 0, -1)            # silent bar, keeps the beat state
REST_TOK = 4                  # FLAT + degree IV (F in C) = rest (was = E major, spare)
SET_ESC = 7                   # settings token 7: escape, one more token follows
SET_N = 9                     # settings codes 0..8
last_set = 0                  # unpack(): the stream's settings code


def set_parts(k: int) -> int:
    """Capacity of k Floppies (1..4): the stream is the key + 34 * k tokens
    (an extra copy's 4-bit part marker is not part of the stream).
    Returns the previous count."""
    global CAP_BITS, BUDGET
    old = (CAP_BITS - KEY_BITS) // PART_BITS
    CAP_BITS = KEY_BITS + PART_BITS * k
    BUDGET = CAP_BITS - KEY_BITS
    return old


class parts:
    """with parts(k): capacity of k Floppies."""
    def __init__(self, k: int):
        self.k = k

    def __enter__(self):
        self.old = set_parts(self.k)

    def __exit__(self, *a):
        set_parts(self.old)


def beat_at(bars: list, q: int) -> int:
    """Beat state before bars[q]: last non-rest bar's beat, else beat 1."""
    for b in reversed(bars[:q]):
        if b != REST:
            return b[2]
    return 0


def last_section(bars: list, q: int, r: int, rh: list | None = None):
    """Last run of bars[:q] played with rhythm r (rests count as the rhythm
    state they play in): (start, end) or None."""
    end = None
    for i in range(q - 1, -1, -1):
        ri = rh[i] if rh is not None else beat_at(bars, i + 1)
        if end is None:
            if ri == r:
                end = i + 1
        elif ri != r:
            return i + 1, end
    return None if end is None else (0, end)


def bar_name(b: Bar) -> str:
    if b == REST:
        return "Rest"
    return NAMES[b[0]] + ("m" if b[1] else "") + f"/{b[2] + 1}"


# ---------------------------------------------------------------- chords

def chord_code(key: int, root: int, minor: int) -> tuple[list[int], int]:
    """Events (FLAT, SWITCH) + degree token 1..7 for a chord in a key."""
    rel = (root - key) % 12
    evs = []
    if rel in DEG_OFS:
        d = DEG_OFS.index(rel)
        dflt = DEG_MIN[d]
    else:                                     # rel 1, 3, 6, 8, 11
        d = DEG_OFS.index((rel + 1) % 12)
        dflt = 0
        evs.append(E_FLAT)
    if minor != dflt:
        evs.append(E_SWITCH)
    return evs, d + 1


def bar_tokens(key: int, b: Bar, beat_before: int) -> list[int]:
    if b == REST:                             # beat unchanged, no NEXT
        return [EV, E_FLAT, REST_TOK]
    toks = []
    steps = (b[2] - beat_before) % 3
    if PREV_EV and SECT_CODE != E_PREV and steps == 2:
        toks += [EV, E_PREV]
        steps = 0
    for _ in range(steps):
        toks += [EV, E_NEXT]
    evs, deg = chord_code(key, b[0], b[1])
    for e in evs:
        toks += [EV, e]
    return toks + [deg]


# ---------------------------------------------------------------- packer

@lru_cache(maxsize=None)
def rep_plan(k: int, copy_bits: int, rep_mode: str) -> tuple[int, tuple]:
    """Cheapest way to play the last bar k more times: (bits, ops).
    op -1 = re-emit the bar, else (event, count token or None)."""
    if k == 0:
        return 0, ()
    best = None
    c, ops = rep_plan(k - 1, copy_bits, rep_mode)
    best = (c + copy_bits, ops + (-1,))
    if rep_mode == "none":
        pass
    elif rep_mode in ("23", "3"):
        for ev, n in ((E_REP2, 2), (E_REP3, 3)) if rep_mode == "23" else ((E_REP3, 3),):
            if k >= n:
                c, ops = rep_plan(k - n, copy_bits, rep_mode)
                if c + EVB < best[0]:
                    best = (c + EVB, ops + ((ev, None),))
    else:
        for n in range(2, 10):
            if k >= n:
                c, ops = rep_plan(k - n, copy_bits, rep_mode)
                if c + EVB + TOK_BITS < best[0]:
                    best = (c + EVB + TOK_BITS, ops + ((E_REPN, n - 2),))
    return best


def run_tokens(key: int, bars: list, p: int, r: int, beat: int, rep_mode: str):
    """Tokens for bars[p:p+r] (all equal), entry beat `beat`: (bits, tokens)."""
    first = bar_tokens(key, bars[p], beat)
    copy = bar_tokens(key, bars[p], bars[p][2])
    c, ops = rep_plan(r - 1, len(copy) * TOK_BITS, rep_mode)
    toks = list(first)
    for op in ops:
        if op == -1:
            toks += copy
        else:
            toks += [EV, op[0]] + ([] if op[1] is None else [op[1]])
    return len(first) * TOK_BITS + c, toks


def run_len(bars: list, p: int) -> int:
    r = 1
    while p + r < len(bars) and bars[p + r] == bars[p]:
        r += 1
    return r


def plain_table(key: int, bars: list, p: int, end: int, beat: int, rep_mode: str):
    """Forward DP over bars[p:end] with runs only. best[j] = (bits, tokens)
    for bars[p:j], entry beat `beat`."""
    best = {p: (0, [])}
    for q in range(p, end):
        if q not in best:
            continue
        b0 = beat if q == p else beat_at(bars, q)
        for r in range(1, min(run_len(bars, q), end - q) + 1):
            c, t = run_tokens(key, bars, q, r, b0, rep_mode)
            cand = (best[q][0] + c, best[q][1] + t)
            if cand[0] > BUDGET:
                break
            j = q + r
            if j not in best or cand[0] < best[j][0]:
                best[j] = cand
    return best


def set_tokens(settings: int) -> list:
    """Settings tokens of code 0..8 (first in the stream, 3 or 6 bits)."""
    return [settings] if settings < SET_ESC else [SET_ESC, settings - SET_ESC]


def pack_key(key: int, bars: list, rep_mode: str = "3", loops="copy", settings: int = 0):
    """Cheapest token list for one key (no key header): (bits, tokens)."""
    n = len(bars)
    rh = [beat_at(bars, i + 1) for i in range(n)]
    pre = set_tokens(settings)
    best = {0: (TOK_BITS * len(pre), pre)}
    for p in range(n):
        if p not in best:
            continue
        beat = beat_at(bars, p)
        table = plain_table(key, bars, p, n, beat, rep_mode)
        # runs (and plain sequences) starting at p: one step of the table
        for j, (c, t) in table.items():
            if j > p and best[p][0] + c <= BUDGET and (j not in best or best[p][0] + c < best[j][0]):
                best[j] = (best[p][0] + c, best[p][1] + t)
        if SECT_CODE is not None:
            for r in range(3):
                sec = last_section(bars, p, r, rh)
                if sec is None:
                    continue
                N = sec[1] - sec[0]
                j = p + N
                cost = best[p][0] + EVB + TOK_BITS
                if (j <= n and bars[p:j] == bars[sec[0]:sec[1]] and cost <= BUDGET
                        and (j not in best or cost < best[j][0])):
                    best[j] = (cost, best[p][1] + [EV, SECT_CODE, r])
        if FAR_COPY:
            for src in range(max(0, p - 64), p):
                m = 0
                while p + m < n and m < 64 and bars[src + m] == bars[p + m]:
                    m += 1
                cost = best[p][0] + EVB + 5 * TOK_BITS
                D = p - src - 1
                for N in range(2, m + 1):
                    j = p + N
                    if cost <= BUDGET and (j not in best or cost < best[j][0]):
                        best[j] = (cost, best[p][1] + [EV, E_FLAT, FAR_TOK, D & 7, D >> 3, (N - 1) & 7, (N - 1) >> 3])
        if loops == "copy":
            for li, N in (enumerate(range(1, 65)) if COPY_WIDE else enumerate(COPY_LENS)):
                if N > p:
                    break
                src = bars[p - N:p]
                cnt = 0
                while cnt < 8 and p + (cnt + 1) * N <= n and bars[p + cnt * N:p + (cnt + 1) * N] == src:
                    cnt += 1
                    j = p + cnt * N
                    cost = best[p][0] + EVB + (3 if COPY_WIDE else 2) * TOK_BITS
                    if cost <= BUDGET and (j not in best or cost < best[j][0]):
                        best[j] = (cost, best[p][1] + [EV, E_COPY] + ([li & 7, li >> 3] if COPY_WIDE else [li]) + [cnt - 1])
            continue
        if not loops:
            continue
        for L in range(1, (n - p) // 2 + 1):
            body = bars[p:p + L]
            c_max = 1
            while (c_max < LOOP_MAX and p + (c_max + 1) * L <= n
                   and bars[p + c_max * L:p + (c_max + 1) * L] == body):
                c_max += 1
            if c_max < 2:
                continue
            if p + L not in table:
                continue
            bc, bt = table[p + L]
            for cnt in range(2, c_max + 1):
                j = p + cnt * L
                cost = best[p][0] + LOOP_BITS + bc
                if cost <= BUDGET and (j not in best or cost < best[j][0]):
                    best[j] = (cost, best[p][1] + [EV, E_LBEGIN] + bt + [EV, E_LEND, cnt - 2])
    return best.get(n, (1 << 30, None))


def pack(bars: list, rep_mode: str = "3", loops="copy", settings: int = 0):
    global PREV_EV
    PREV_EV = loops == "copy"
    """-> (key, tokens, bits used incl. key) for the cheapest key, or None
    if the song doesn't fit. settings: code 0..8, its tokens first."""
    best = None
    for key in range(12):
        c, t = pack_key(key, bars, rep_mode, loops, settings)
        if best is None or c < best[2] - KEY_BITS:
            best = (key, t, c + KEY_BITS)
    if best[2] > CAP_BITS:
        return None
    return best


def to_bits(key: int, toks: list) -> int:
    v = key
    for i, t in enumerate(toks):
        v |= t << (KEY_BITS + TOK_BITS * i)
    assert v < 1 << CAP_BITS
    return v


# ---------------------------------------------------------------- unpacker

def unpack(v: int, rep_mode: str = "3", loops="copy") -> list:
    """Floppy bits -> bar list. Tolerates junk (stops at MAX_BARS). The
    settings code -> last_set."""
    global last_set
    last_set = 0
    key = v & 15
    if key > 11:
        key = 0
    ntok = (CAP_BITS - KEY_BITS) // TOK_BITS
    toks = [(v >> (KEY_BITS + TOK_BITS * i)) & 7 for i in range(ntok)]
    bars = []
    pc, beat, flat, sw = 0, 0, 0, 0
    loop_pc, loop_beat, left = -1, 0, -1

    def tok():
        nonlocal pc
        if pc >= ntok:
            return None
        pc += 1
        return toks[pc - 1]

    s = tok()                                 # settings: one token, or 7 + one more
    if s == SET_ESC:
        e = tok()
        s = SET_ESC + e if e < SET_N - SET_ESC else 0
    last_set = s
    while len(bars) < MAX_BARS:
        t = tok()
        if t is None:
            break
        if t != EV:
            d = t - 1
            if FAR_COPY and flat and t == FAR_TOK:
                v4 = [tok() for _ in range(4)]
                if None in v4:
                    break
                D, N = v4[0] | v4[1] << 3, (v4[2] | v4[3] << 3) + 1
                src = len(bars) - D - 1
                if src >= 0:
                    for k in range(N):
                        bars.append(bars[src + k])
                    beat = beat_at(bars, len(bars))
                flat = sw = 0
                continue
            if flat and t == REST_TOK:
                bars.append(REST)
                flat = sw = 0
                continue
            root = (key + DEG_OFS[d] - flat) % 12
            minor = (0 if flat else DEG_MIN[d]) ^ sw
            bars.append((root, minor, beat))
            flat = sw = 0
            continue
        e = tok()
        if e is None or e == E_END:
            break
        if SECT_CODE is not None and e == SECT_CODE:
            r = tok()
            if r is None:
                break
            sec = last_section(bars, len(bars), r)
            if sec is not None:
                bars += bars[sec[0]:sec[1]]
                beat = beat_at(bars, len(bars))
            continue
        if e == E_SWITCH:
            sw ^= 1
        elif e == E_FLAT:
            flat = 1
        elif e == E_NEXT:
            beat = (beat + 1) % 3
        elif loops == "copy":
            if e == E_PREV:
                beat = (beat + 2) % 3
            elif e == E_COPY:
                li = tok()
                if COPY_WIDE and li is not None:
                    hi = tok()
                    li = None if hi is None else li | hi << 3
                c = tok()
                if c is None or li is None:
                    break
                N = li + 1 if COPY_WIDE else COPY_LENS[li]
                if N <= len(bars):
                    bars += bars[-N:] * (c + 1)
                    beat = beat_at(bars, len(bars))
            elif e in (E_REP2, E_REP3) and bars:
                if rep_mode in ("23", "3"):
                    bars += [bars[-1]] * (2 if e == E_REP2 else 3)
                elif e == E_REPN:
                    c = tok()
                    if c is None:
                        break
                    bars += [bars[-1]] * (c + 2)
        elif e in (E_REP2, E_REP3) and bars:
            if rep_mode == "23":
                n = 2 if e == E_REP2 else 3
            elif e == E_REPN:
                c = tok()
                if c is None:
                    break
                n = c + 2
            else:
                continue
            bars += [bars[-1]] * n
        elif e == E_LBEGIN and loop_pc < 0:
            loop_pc, loop_beat, left = pc, beat, -1
        elif e == E_LEND:
            c = tok()
            if c is None:
                break
            if loop_pc < 0:
                continue
            if left < 0:
                left = c + 1                  # replays still to play
            if left > 0:
                left -= 1
                pc, beat = loop_pc, loop_beat
            else:
                loop_pc = -1
    return bars[:MAX_BARS]


# ---------------------------------------------------------------- editor

def shown_limit(bars: list, used_bits: int) -> int:
    """Bar knob goes up to this, no further (worst case 3 bits/bar)."""
    return min(MAX_BARS, len(bars) + (CAP_BITS - used_bits) // TOK_BITS)


CHORD_REST = 24               # Chord knob: 0..23 = root * 2 + minor, 24 = "Rest"
CHORD_END = 25                # 25 = "End"
SEL_CLEAR = 0                 # Bar selector: 0 = "Clear", 1..128 = bars


def set_bar(bars: list, n: int, bar: Bar | None):
    """Set bar n (0-based); bar None ("End") cuts the song before bar n.
    A gap after the song end is filled with copies of the last bar.
    -> (new bars, pack result) or None if it doesn't fit."""
    if bar is None:
        new = list(bars[:n])
        return new, pack(new)             # shorter always fits
    new = list(bars)
    if n >= len(new):
        fill = new[-1] if new else (0, 0, 0)
        new += [fill] * (n + 1 - len(new))
    new[n] = bar
    res = pack(new)
    return None if res is None else (new, res)


# ---------------------------------------------------------------- tests

def chords(spec: str, beat: int = 0) -> list:
    """'G D Em C' -> bars."""
    out = []
    for s in spec.split():
        m = s.endswith("m")
        out.append((NAMES.index(s[:-1] if m else s), int(m), beat))
    return out


def roundtrip(bars: list, rep_mode: str = "3", loops="copy") -> int | None:
    res = pack(bars, rep_mode, loops)
    if res is None:
        return None
    key, toks, used = res
    got = unpack(to_bits(key, toks), rep_mode, loops)
    assert got == bars, (bars, got, key, toks)
    return used


def random_song(rng: random.Random) -> list:
    """Phrases of 1..4 bars repeated, like real songs."""
    key = rng.randrange(12)
    pool = [((key + DEG_OFS[d]) % 12, DEG_MIN[d]) for d in range(7)]
    pool += [(rng.randrange(12), rng.randrange(2))]
    bars = []
    while len(bars) < rng.randrange(1, 60):
        beat = rng.randrange(3)
        phrase = []
        for _ in range(rng.randrange(1, 5)):
            r, m = rng.choice(pool)
            phrase += [(r, m, beat) if rng.random() > 0.1 else REST] * rng.choice((1, 1, 2, 4))
        bars += phrase * rng.randrange(1, 5)
    return bars[:MAX_BARS]


class legacy:
    """with legacy(): SECT off (for the old REP2 / REPN / loop modes)."""
    def __enter__(self):
        global SECT_CODE
        self.old, SECT_CODE = SECT_CODE, None

    def __exit__(self, *a):
        global SECT_CODE
        SECT_CODE = self.old


def main() -> None:
    assert unpack(0) == [] and last_set == 0  # empty Floppy = empty song, default settings
    rng = random.Random(1)
    for s in range(SET_N):                    # settings codes round trip (3 or 6 bits)
        key, toks, used = pack(chords("G D Em C"), settings=s)
        assert unpack(to_bits(key, toks)) == chords("G D Em C") and last_set == s
        assert used == pack(chords("G D Em C"))[2] + (3 if s >= SET_ESC else 0)
    for e in range(SET_N - SET_ESC, 8):       # 7 + unused values read as the defaults
        unpack(to_bits(0, [SET_ESC, e]))
        assert last_set == 0
    fits = 0
    for i in range(300):
        if roundtrip(random_song(rng)) is not None:
            fits += 1
        n = rng.randrange(1, 40)              # unstructured: any chord, any rhythm
        bars = [(rng.randrange(12), rng.randrange(2), rng.randrange(3)) if rng.random() > 0.1 else REST
                for _ in range(n)]
        roundtrip(bars)
    print(f"round trips ok ({fits} of 300 structured songs fit)")
    for k in (2, 3, 4):                       # more Floppies: longer songs fit
        with parts(k):
            fits = 0
            for i in range(60):
                bars = [(rng.randrange(12), rng.randrange(2), rng.randrange(3)) for _ in range(rng.randrange(20, 90))]
                fits += roundtrip(bars) is not None
                roundtrip((random_song(rng) * 2)[:MAX_BARS])
        print(f"{k} Floppies ({KEY_BITS + PART_BITS * k} bits): round trips ok, "
              f"{fits} of 60 random 20..89-bar songs fit")

    verse = chords("Am F C G") * 2
    chorus = chords("F G C Am", 1) * 2
    bridge = chords("Dm Dm F G", 2)
    songs = {
        "I-V-vi-IV in G, 1 bar each, x4": chords("G D Em C") * 4,
        "I-V-vi-IV in G, 4 bars each": [b for b in chords("G D Em C") for _ in range(4)],
        "D C G (I-bVII-IV), 2 bars each, x4": [b for b in chords("D C G G") for _ in range(2)] * 4,
        "12-bar blues in A, x2": chords("A D A A D D A A E D A E") * 2,
        "Am F C G + E (V of Am), x4": chords("Am F C E") * 4,
        "verse x2 + chorus x2 (rhythm 2), x2": (verse + chorus) * 2,
        "V C V C bridge C C": (verse + chorus) * 2 + bridge + chorus * 2,
        "verse/chorus, 4 bars per chord": [b for b in (verse[:4] + chorus[:4]) * 2 for _ in range(4)],
        "one chord, 128 bars": chords("E") * 128,
    }
    print(f"\n{'song':40} bars   bits  old(REP2)  no blocks  key  shown")
    for name, bars in songs.items():
        u = roundtrip(bars)
        with legacy():
            old = roundtrip(bars, "23")
            nl = pack(bars, "23", loops=False)
        key = pack(bars)[0] if u else None
        lim = shown_limit(bars, u) if u else "-"
        print(f"{name:40} {len(bars):4}  {u or 'FULL':>5}  {old or 'FULL':>9}  "
              f"{nl[2] if nl else 'FULL':>9}  {NAMES[key] if key is not None else '-':>3}  {lim:>5}")

    # how many bars of distinct chords fit (worst realistic case)
    for desc, pick in (("in-key chords", lambda i: chords("C Dm Em F G Am A#")[rng.randrange(7)]),
                       ("any chord (24)", lambda i: (rng.randrange(12), rng.randrange(2), 0))):
        n = 0
        bars = []
        while True:
            b = pick(n)
            if bars and b == bars[-1]:
                n += 1
                continue
            if pack(bars + [b]) is None:
                break
            bars.append(b)
            n += 1
        print(f"max bars, every bar a new chord, {desc}: {len(bars)}")


if __name__ == "__main__":
    sys.exit(main())
