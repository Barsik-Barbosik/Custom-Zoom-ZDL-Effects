"""DrumBox beats and fills: patterns.json is the source (made for an editor).

This module reads and checks patterns.json (format: the comment above
PATTERNS_JSON) and build.py turns it into drumbox_patterns.h; preview.py
and the other tools import the lists from here (BEATS, FILLS, defaults,
groups).

The sequencer plays a song of two sections in turn:
  section A  BeatA for LenA bars, FillA laid over the end of its last bar
  section B  BeatB for LenB bars, FillB laid over the end of its last bar
A crash fill puts a crash on beat 1 of the next section (FillB's also
when the footswitch starts the song), unless the last crash is less than
4 bars back (DB_CRASH_GAP; short sections crash every 4th bar at most);
a middle fill (Len ..M) never crashes. Beat 0 "None" plays no beat (the
fills play over silence), beat 1 "Randm" a random beat, a new one at every
start of its section; fill 0 "None" is the plain beat, fill 1 "Randm" a
random fill per fill bar. Beats carry no crash of their own: the crash
comes with the fills (a crash on every phrase start gets annoying).

  python -B patterns.py              # check, write drumbox_patterns.h
  python -B patterns.py --export P   # patterns.json rewritten in its layout to P

Lengths, swing amounts and the random seeds stay here (the DSP's laws).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

WORDS = 5               # words per bar
LANES = "KSHORCTAL"     # word k of a bar = lanes 2k | 2k+1 << 16 (L alone in word 4)
STEPS = 16
DEFAULT_ACCENT = "x...x...x...x..."
MAX_BEATS = 151         # knob 0..150 (the LineSel handler law stops at 150)
MAX_FILLS = 151

NONE = "................"


# Swing per beat (stored in bits 16-17 of beat word 4, which holds only the
# low tom): name -> (code, s, step bit). A step whose bit is set lasts
# 1 - s sixteenths, the others 1 + s (pairs keep the tempo).
SWINGS = {
    "straight": (0, 0.0, 1),
    "sw16": (1, 0.17, 1),       # light 16th swing (MPC ~58 %)
    "sw16h": (2, 0.26, 1),      # heavy 16th swing (~1.7:1)
    "tri8": (3, 1.0 / 3.0, 2),  # 8th triplet shuffle (2:1)
}


class Lanes(dict):
    """A beat's lanes (lane letter -> 16-step string) plus its swing code."""
    swing = 0


def P(name: str, short: str, swing: str = "straight",
      **lanes: str) -> tuple[str, str, dict[str, str]]:
    unknown = set(lanes) - set(LANES)
    if unknown:
        raise ValueError(f"beat {name}: unknown lanes {unknown}")
    if swing not in SWINGS:
        raise ValueError(f"beat {name}: unknown swing {swing!r}")
    L = Lanes()
    L.swing = SWINGS[swing][0]
    for l in LANES:
        s = lanes.get(l, DEFAULT_ACCENT if l == "A" else NONE)
        if len(s) != STEPS or set(s) - {"x", "."}:
            raise ValueError(f"beat {name}: lane {l} must be 16 of 'x'/'.': {s!r}")
        L[l] = s
    return (name, short, L)


# Beat 1 Randm is not a pattern: at every start of its section (and when
# the knob is turned to it) the DSP picks one of all the real beats 2..N-1
# with its own xorshift32 (resolve_beat(), db_pick_beat() in drumbox.c).
# Stored as all zero.
BEAT_RANDOM = 1
BEAT_RANDOM_FIRST = 2
BEAT_RNG_SEED = 0x6C8E9CF5


# --- Fills --------------------------------------------------------------

def F(name: str, short: str, w: int, replace: str = "", crash: bool = False,
      **lanes: str) -> tuple[str, str, int, str, dict[str, str], bool]:
    if not 0 <= w <= 15:
        raise ValueError(f"fill {name}: window start {w} not in 0..15")
    unknown = (set(lanes) | set(replace)) - set(LANES)
    if unknown:
        raise ValueError(f"fill {name}: unknown lanes {unknown}")
    n = STEPS - w
    L: dict[str, str] = {}
    for l in LANES:
        s = lanes.get(l, "." * n)
        if len(s) != n or set(s) - {"x", "."}:
            raise ValueError(f"fill {name}: lane {l} must be {n} of 'x'/'.': {s!r}")
        L[l] = "." * w + s
    rep = set(replace)
    if rep & {"H", "O"} or "x" in L["H"] or "x" in L["O"]:
        rep |= {"H", "O"}
    if rep & {"T", "L"} or "x" in L["T"] or "x" in L["L"]:
        rep |= {"T", "L"}
    return (name, short, w, "".join(l for l in LANES if l in rep), L, crash)


# --- patterns.json: read and write -----------------------------------------
#
# {"format": "drumbox-patterns", "version": 1,
#  "defaults": {"BeatA": SCREEN, "BeatB": .., "FillA": .., "FillB": ..},
#  "beats": [{"group": TITLE, "entries": [BEAT, ..]}, ..],
#  "fills": [{"group": TITLE, "entries": [FILL, ..]}, ..]}
# BEAT = {"screen", "name", ["swing"], "lanes"}, FILL = {"screen", "name",
# "w", ["replace"], ["crash": true], ["norandom": true], "lanes"}.
#   screen  1..5 printable characters, no spaces, unique per list (knob text)
#   name    long name (README)
#   lanes   lane letter -> 16 steps, 'x' hit, '.' rest: K kick, S snare,
#           H closed hat, O open hat, R ride, C crash, T high tom, L low tom,
#           A accent (accented steps play louder). Lanes left out are empty,
#           except a beat's A: left out = every beat (x...x...x...x...).
#   swing   sw16 / sw16h delay every odd 16th (light ~1.4:1 / heavy ~1.7:1);
#           tri8 moves the second 8th of each beat to its last triplet (2:1
#           shuffle, steps 0, 1, 2 of a beat = its triplet 8ths; avoid step
#           3). Left out = straight. Beats keep C empty: crashes come with
#           the fills.
#   w       a fill is laid over the beat from step w to 15; its lanes have
#           no hits before w
#   replace beat lanes cleared inside the window (a fill that writes or
#           clears H or O replaces both, T or L both toms); other lanes keep
#           the beat and add the fill's hits
#   crash   crash on beat 1 of the next bar
#   norandom never picked by Randm; these fills must be the last ones
# Beats 0, 1 and fills 0, 1 are None and Randm (no lanes). At most 151 of
# each; the knob shows them in this order, groups = the README's tables.
# Written as JSON.stringify(data, null, 1) would write it (CRLF here).

PATTERNS_JSON = Path(__file__).resolve().parent / "patterns.json"
FORMAT = "drumbox-patterns"
FORMAT_VERSION = 1
_DEFAULT_KEYS = ("BeatA", "BeatB", "FillA", "FillB")


def to_data(beats, fills, beat_groups, fill_groups, defaults: dict[str, str],
            no_random) -> dict:
    """The lists as patterns.json's object."""
    swing_name = {code: n for n, (code, _s, _b) in SWINGS.items()}

    def grouped(entries, groups, one):
        starts, out = dict(groups), []
        for i, e in enumerate(entries):
            if i in starts or not out:
                out.append({"group": starts.get(i, ""), "entries": []})
            out[-1]["entries"].append(one(e))
        return out

    def beat(e):
        name, short, lanes = e
        d = {"screen": short, "name": name}
        if lanes.swing:
            d["swing"] = swing_name[lanes.swing]
        d["lanes"] = {l: s for l, s in lanes.items()
                      if ((s != DEFAULT_ACCENT) if l == "A" else ("x" in s))}
        return d

    def fill(e):
        name, short, w, rep, lanes, crash = e
        d = {"screen": short, "name": name, "w": w}
        if rep:
            d["replace"] = rep
        if crash:
            d["crash"] = True
        if short in no_random:
            d["norandom"] = True
        d["lanes"] = {l: s for l, s in lanes.items() if "x" in s}
        return d

    return {"format": FORMAT, "version": FORMAT_VERSION,
            "defaults": {k: defaults[k] for k in _DEFAULT_KEYS},
            "beats": grouped(beats, beat_groups, beat),
            "fills": grouped(fills, fill_groups, fill)}


def dump_json(data: dict) -> str:
    return json.dumps(data, indent=1, ensure_ascii=False).replace("\n", "\r\n") + "\r\n"


class PatternFile:
    """patterns.json, read and checked (errors name the entry)."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.beats: list = []
        self.fills: list = []
        self.beat_groups: list[tuple[int, str]] = []
        self.fill_groups: list[tuple[int, str]] = []
        self.defaults: dict[str, str] = {}
        self.no_random: list[str] = []
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            raise ValueError(f"{self.path.name}:{e.lineno}: {e.msg}") from None
        if not isinstance(data, dict) or data.get("format") != FORMAT:
            self._fail("top", f'not a DrumBox pattern file ("format": "{FORMAT}")')
        if data.get("version") != FORMAT_VERSION:
            self._fail("top", f"version {data.get('version')!r}, expected {FORMAT_VERSION}")
        defaults = data.get("defaults")
        if not isinstance(defaults, dict) or sorted(defaults) != sorted(_DEFAULT_KEYS):
            self._fail("defaults", f"needs exactly {', '.join(_DEFAULT_KEYS)}")
        self.defaults = dict(defaults)
        for kind, keys in (("beats", ("screen", "name", "swing", "lanes")),
                           ("fills", ("screen", "name", "w", "replace", "crash",
                                      "norandom", "lanes"))):
            groups = data.get(kind)
            if not isinstance(groups, list):
                self._fail(kind, "must be a list of groups")
            for gi, g in enumerate(groups):
                if not isinstance(g, dict) or not isinstance(g.get("entries"), list):
                    self._fail(f"{kind}[{gi}]", 'a group is {"group": TITLE, "entries": [..]}')
                where = f"{kind} group {g.get('group')!r}"
                out = self.beats if kind == "beats" else self.fills
                if g.get("group"):
                    (self.beat_groups if kind == "beats" else self.fill_groups).append(
                        (len(out), str(g["group"])))
                for e in g["entries"]:
                    self._entry(where, kind, keys, e)

    def _fail(self, where: str, msg: str):
        raise ValueError(f"{self.path.name}: {where}: {msg}")

    def _entry(self, where: str, kind: str, keys, e) -> None:
        if not isinstance(e, dict):
            self._fail(where, f"entry {e!r} is not an object")
        where += f", {kind[:-1]} {e.get('screen')!r}"
        extra = set(e) - set(keys)
        if extra:
            self._fail(where, f"unknown keys {sorted(extra)} (keys: {', '.join(keys)})")
        for k in ("screen", "name"):
            if not isinstance(e.get(k), str):
                self._fail(where, f'"{k}" must be a string')
        lanes = e.get("lanes", {})
        if not isinstance(lanes, dict) or not all(isinstance(s, str) for s in lanes.values()):
            self._fail(where, '"lanes" must map lane letters to strings')
        try:
            if kind == "beats":
                self.beats.append(P(e["name"], e["screen"], e.get("swing", "straight"), **lanes))
                return
            w = e.get("w")
            if not isinstance(w, int) or isinstance(w, bool) or not 0 <= w <= 15:
                self._fail(where, '"w" must be 0..15')
            for flag in ("crash", "norandom"):
                if not isinstance(e.get(flag, False), bool):
                    self._fail(where, f'"{flag}" must be true or false')
            cut = {}
            for l, s in lanes.items():
                if len(s) != STEPS or set(s) - {"x", "."}:
                    self._fail(where, f"lane {l} must be 16 of 'x'/'.': {s!r}")
                if "x" in s[:w]:
                    self._fail(where, f"lane {l} has hits before w={w}")
                cut[l] = s[w:]
            self.fills.append(F(e["name"], e["screen"], w, e.get("replace", ""),
                                e.get("crash", False), **cut))
            if e.get("norandom"):
                self.no_random.append(e["screen"])
        except ValueError as err:
            if str(err).startswith(self.path.name):
                raise
            self._fail(where, str(err))

    def data(self) -> dict:
        return to_data(self.beats, self.fills, self.beat_groups, self.fill_groups,
                       self.defaults, self.no_random)

    def text(self) -> str:
        return dump_json(self.data())


_PF = PatternFile(PATTERNS_JSON)
BEATS: list[tuple[str, str, dict[str, str]]] = _PF.beats
FILLS: list[tuple[str, str, int, str, dict[str, str], bool]] = _PF.fills
BEAT_GROUPS: list[tuple[int, str]] = _PF.beat_groups    # (first entry, title)
FILL_GROUPS: list[tuple[int, str]] = _PF.fill_groups
BEAT_RANDOM_COUNT = len(BEATS) - BEAT_RANDOM_FIRST

# Fill 1 is not a pattern: on every fill bar where a fill knob is at Randm,
# the DSP picks one of the real fills 2..2+FILL_RANDOM_COUNT-1 with its own
# xorshift32 (resolve_fill(), db_resolve_fill() in drumbox.c). Stored as all
# zero. The last fills (marked norandom: stops and breaks, which drop the
# beat) are never picked; they must stay at the end.
FILL_RANDOM = 1
FILL_NO_RANDOM = tuple(_PF.no_random)
FILL_RANDOM_COUNT = len(FILLS) - 2 - len(FILL_NO_RANDOM)
FILL_RNG_SEED = 0x2545F491

# Knob defaults (screen names): BeatA / FillA (section A, verse), BeatB /
# FillB (section B, chorus).
DEFAULT_BEAT_A = _PF.defaults["BeatA"]
DEFAULT_BEAT_B = _PF.defaults["BeatB"]
DEFAULT_FILL_A = _PF.defaults["FillA"]
DEFAULT_FILL_B = _PF.defaults["FillB"]


# --- Length ------------------------------------------------------------
# LenA / LenB knobs: (screen name, bars, mid) of each song section. The
# song plays section A (BeatA, LenA bars, FillA on its last bar), then
# section B (BeatB, LenB bars, FillB on its last bar), and repeats.
# Any 1..32 bars work: the DSP counts the bars of each section. Skip = 0
# bars: that section is not played, the other one loops on
# its own (both Skip = no drums). mid: the section's fill also
# plays on the last bar of its first half (4BarM = A, A+F, A, A+F; a crash
# fill there crashes the second half's downbeat).
LENGTHS: list[tuple[str, int, bool]] = [
    ("Skip", 0, False), ("1Bar", 1, False), ("2Bar", 2, False), ("2BarM", 2, True),
    ("4Bar", 4, False), ("4BarM", 4, True), ("8Bar", 8, False), ("8BarM", 8, True),
    ("16Bar", 16, False), ("16BrM", 16, True), ("32Bar", 32, False), ("32BrM", 32, True),
    ("64Bar", 64, False), ("64BrM", 64, True)]
DEFAULT_LEN_A = "8BarM"
DEFAULT_LEN_B = "8BarM"


# --- Bit packing ---------------------------------------------------------

def _mask(s: str) -> int:
    if len(s) != STEPS or set(s) - {"x", "."}:
        raise ValueError(f"bad {STEPS}-step lane: {s!r}")
    return sum(1 << i for i, ch in enumerate(s) if ch == "x")


def _pack(lanes: dict[str, str]) -> list[int]:
    """5 words, two lanes each: [K|S<<16, H|O<<16, R|C<<16, T|A<<16, L]."""
    m = [_mask(lanes[l]) for l in LANES] + [0]
    return [m[2 * k] | m[2 * k + 1] << 16 for k in range(WORDS)]


def _unpack(w: list[int]) -> list[int]:
    """Bar words -> one 16-bit mask per lane, in LANES order."""
    return [(w[i // 2] >> (16 * (i % 2))) & 0xFFFF for i in range(len(LANES))]


def _swing_lines() -> list[str]:
    """DB_SWING_AMT(c) / DB_SWING_MASK(c): s and the step bit of swing code c
    (an if-chain on the 2-bit code from beat word 4, read once per bar)."""
    amt, mask = "0.0f", "1u"
    for _name, (code, sw, bit) in sorted(SWINGS.items(), key=lambda kv: kv[1][0]):
        if code:
            amt = f"((c) == {code} ? {sw:.6f}f : {amt})"
            mask = f"((c) == {code} ? {bit}u : {mask})" if bit != 1 else mask
    return [f"#define DB_SWING_AMT(c) {amt}", f"#define DB_SWING_MASK(c) {mask}"]


def beat_words(idx: int) -> list[int]:
    if idx == BEAT_RANDOM:
        return [0] * WORDS
    w = _pack(BEATS[idx][2])
    w[4] |= BEATS[idx][2].swing << 16
    return w


def fill_words(idx: int) -> list[int]:
    """[header, 5 packed lane words]. header = w | crash-after << 4 |
    replace-lane bits << 8 (bit 8 + k for lane k in K S H O R C T A L order).
    Fill 0 is all zero."""
    _name, _short, w, rep, lanes, crash = FILLS[idx]
    if idx == FILL_RANDOM:
        return [0] * (1 + WORDS)
    if not any("x" in s for s in lanes.values()) and not rep and not crash:
        return [0] * (1 + WORDS)
    hdr = w | (16 if crash else 0) | sum(1 << (8 + LANES.index(l)) for l in rep)
    return [hdr] + _pack(lanes)


def compose(beat: list[int], fill: list[int], prev: list[int]) -> list[int]:
    """Effective bar = beat with fill laid over, plus a crash on beat 1 if the
    previous bar's fill (prev) is a crash fill; same as db_compose_bar()."""
    hdr = fill[0]
    win = (0xFFFF << (hdr & 15)) & 0xFFFF
    m = hdr >> 8
    out = []
    for i in range(WORDS):
        clear = (win if m >> (2 * i) & 1 else 0) | ((win << 16) if m >> (2 * i + 1) & 1 else 0)
        out.append((beat[i] & ~clear & 0xFFFFFFFF) | fill[1 + i])
    if prev[0] & 16:
        out[2] |= 1 << 16            # crash on step 0
    return out


def resolve_fill(fill: int, rng: int) -> tuple[int, int]:
    """Knob fill -> fill to play, and the new random state. Randm picks one of
    the fills 2..2+G-1 (G = FILL_RANDOM_COUNT, all but FILL_NO_RANDOM):
    xorshift32, then 2 + ((rng >> 16) * G) >> 16 (integer only, so C and
    Python agree exactly). Same as db_resolve_fill()."""
    if fill != FILL_RANDOM:
        return fill, rng
    rng ^= (rng << 13) & 0xFFFFFFFF
    rng ^= rng >> 17
    rng ^= (rng << 5) & 0xFFFFFFFF
    return 2 + (((rng >> 16) * FILL_RANDOM_COUNT) >> 16), rng


def resolve_beat(rng: int) -> tuple[int, int]:
    """Randm beat -> a groove to play, and the new random state: xorshift32,
    then BEAT_RANDOM_FIRST + ((rng >> 16) * G) >> 16 (integer only, so C and
    Python agree exactly). Same as db_pick_beat()."""
    rng ^= (rng << 13) & 0xFFFFFFFF
    rng ^= rng >> 17
    rng ^= (rng << 5) & 0xFFFFFFFF
    return BEAT_RANDOM_FIRST + (((rng >> 16) * BEAT_RANDOM_COUNT) >> 16), rng


def masks_of(beat: int, fill: int, prev: int) -> list[int]:
    """[K, S, H, O, R, C, T, A, L] 16-bit masks of a bar: beat with fill laid
    over, crash on beat 1 if prev (the fill played in the bar before) is a
    crash fill. fill / prev are played fills (never Randm)."""
    return _unpack(compose(beat_words(beat), fill_words(fill), fill_words(prev)))


def bar_swing(beat: int, fill: int, prev: int) -> tuple[float, int]:
    """(s, step bit) of a bar from its composed word 4, like drumbox.c's
    DB_SWING_AMT / DB_SWING_MASK after db_compose_bar (s as float32 there)."""
    c = (compose(beat_words(beat), fill_words(fill), fill_words(prev))[4] >> 16) & 3
    for code, sw, bit in SWINGS.values():
        if code == c:
            return sw, bit
    raise ValueError(f"swing code {c}")


def section_fill(bar: int, bars: int, fill: int, mid: bool = False) -> int:
    """Fill of bar `bar` (0..bars-1) of a section: its fill on the last bar
    (and with mid on the first half's last bar), else none. Same as
    db_section_fill() in drumbox.c."""
    return fill if bar == bars - 1 or (mid and bar == bars // 2 - 1) else 0


def knob_to_length(v: int) -> int:
    return min(max(v, 0), len(LENGTHS) - 1)


def knob_to_beat(v: int) -> int:
    """Same mapping as drumbox.c: knob value -> beat index."""
    return min(max(v, 0), len(BEATS) - 1)


def knob_to_fill(v: int) -> int:
    return min(max(v, 0), len(FILLS) - 1)


def length_index(short: str) -> int:
    return [ph[0] for ph in LENGTHS].index(short)


def beat_index(short: str) -> int:
    return [b[1] for b in BEATS].index(short)


def fill_index(short: str) -> int:
    return [f[1] for f in FILLS].index(short)


def _check_names(kind: str, names: list[tuple[str, str]]) -> None:
    seen: dict[str, str] = {}
    for name, short in names:
        if not (1 <= len(short) <= 5) or not all(33 <= ord(c) < 127 for c in short):
            raise ValueError(f"{kind} {name}: screen name {short!r} needs 1..5 printable ASCII, no spaces")
        if short in seen:
            raise ValueError(f"{kind} screen name {short!r} used by {seen[short]!r} and {name!r}")
        seen[short] = name


def _validate() -> None:
    if not 2 <= len(BEATS) <= MAX_BEATS:
        raise ValueError(f"need 2..{MAX_BEATS} beats, have {len(BEATS)}")
    if not 2 <= len(FILLS) <= MAX_FILLS:
        raise ValueError(f"need 2..{MAX_FILLS} fills, have {len(FILLS)}")
    _check_names("beat", [(b[0], b[1]) for b in BEATS])
    _check_names("fill", [(f[0], f[1]) for f in FILLS])
    if BEATS[BEAT_RANDOM][1] != "Randm" or BEATS[0][1] != "None":
        raise ValueError("beats 0 and 1 must be None and Randm")
    bsigs: dict[tuple, str] = {}
    for idx, (name, _s, lanes) in enumerate(BEATS):
        if idx == BEAT_RANDOM:
            continue
        if _mask(lanes["H"]) & _mask(lanes["O"]):
            raise ValueError(f"beat {name}: closed hat chokes open hat on the same step")
        sig = tuple(beat_words(idx))
        if sig in bsigs:
            raise ValueError(f"beat {name} is identical to {bsigs[sig]}")
        bsigs[sig] = name
    sigs: dict[tuple, str] = {}
    for idx, (name, _s, _w, _r, lanes, _c) in enumerate(FILLS):
        if idx == FILL_RANDOM:
            continue
        if _mask(lanes["H"]) & _mask(lanes["O"]):
            raise ValueError(f"fill {name}: closed hat chokes open hat on the same step")
        sig = tuple(fill_words(idx))
        if sig in sigs:
            raise ValueError(f"fill {name} is identical to {sigs[sig]}")
        sigs[sig] = name
    if tuple(f[1] for f in FILLS[len(FILLS) - len(FILL_NO_RANDOM):]) != FILL_NO_RANDOM:
        raise ValueError(f"FILL_NO_RANDOM {FILL_NO_RANDOM} must be the last fills")
    if fill_words(0) != [0] * (1 + WORDS):
        raise ValueError("fill 0 must be empty (plain A)")
    beat_index(DEFAULT_BEAT_A), beat_index(DEFAULT_BEAT_B)
    fill_index(DEFAULT_FILL_A), fill_index(DEFAULT_FILL_B)
    _check_names("length", [(n, n) for n, _b, _m in LENGTHS])
    for n, b, mid in LENGTHS:
        if not 0 <= b <= 64:
            raise ValueError(f"length {n}: bars must be 0 (Skip)..64")
        if mid and (b < 2 or b % 2):
            raise ValueError(f"length {n}: a middle fill needs an even bar count >= 2")
    for i, (n, b, mid) in enumerate(LENGTHS):   # DB_LENGTH_FROM_KNOB's law
        if (b, mid) != ((i, False) if i <= 1 else (1 << (i >> 1), bool(i & 1))):
            raise ValueError(f"length {n}: entry {i} breaks the 0, 1, 2^(i/2) (+M if odd) law")
    length_index(DEFAULT_LEN_A), length_index(DEFAULT_LEN_B)


def _getstr_table_lines(macro: str, table: str, shorts: list[str],
                        index: str | None = None) -> list[str]:
    """GetString body: knob value v -> screen name in s, from a `static
    const` name table, 5 bytes per name (<= 5 chars, NUL-padded; the 6th
    byte is always NUL, so it isn't stored). The length is the number of
    non-zero bytes. The code loads the table address with MVKL/MVKH, which
    the linker gives a .rela.dyn entry so the pedal fixes it when it places
    .const (stock effects do the same).
    `index` maps v to the entry (default: v, values past the end show the
    last name)."""
    n = len(shorts)
    data = b"".join(sh.encode("ascii") + bytes(5 - len(sh)) for sh in shorts)
    for sh in shorts:
        if not 1 <= len(sh) <= 5 or not sh.isprintable():
            raise ValueError(f"screen name {sh!r}: 1..5 characters")
    if index is None:
        index = f"((v) >= {n - 1}u ? {n - 1}u : (v))"
    out = [f"static const uint8_t {table}[{len(data)}] = {{ /* {', '.join(shorts)} */"]
    for i in range(0, len(data), 20):
        out.append("    " + ", ".join(f"0x{x:02X}" for x in data[i:i + 20]) + ",")
    out += ["};",
            f"#define {macro}(v, s) do {{ \\",
            f"    const uint8_t *p_ = &{table}[5u * (unsigned int){index}]; \\",
            "    unsigned int w0_ = _mem4_const(p_); \\",
            "    unsigned int b4_ = p_[4]; \\",
            "    _mem4((void *)(s)) = w0_; \\",
            "    (s)[4] = (char)b4_; \\",
            "    (s)[5] = 0; \\",
            "    return (int)(((w0_ & 0xFFu) != 0u) + ((w0_ & 0xFF00u) != 0u) + \\",
            "                 ((w0_ & 0xFF0000u) != 0u) + ((w0_ >> 24) != 0u) + (b4_ != 0u)); \\",
            "} while (0)"]
    return out


def write_header(out_path: str | Path) -> None:
    _validate()
    nb, nf = len(BEATS), len(FILLS)
    lines = [
        "/* Generated by patterns.py. Do not edit by hand. */",
        "#ifndef DRUMBOX_PATTERNS_H",
        "#define DRUMBOX_PATTERNS_H",
        "",
        "#include <stdint.h>",
        "",
        "/* Beats: 5 words each, two 16-step lanes per word (bit n = step n):",
        " *   [0] kick | snare << 16     [1] closed hat | open hat << 16",
        " *   [2] ride | crash << 16     [3] high tom | accent << 16",
        " *   [4] low tom | swing code << 16 (beats only, SWINGS in patterns.py)",
        " * Fills: 6 words each: [0] header = window start w (bits 0-3) |",
        " * crash on the next bar's beat 1 (bit 4) |",
        " * replace lanes << 8 (bit 8 + k, k in K S H O R C T A L order), then 5",
        " * lane words as above (hits inside the window only). Fill 0 is empty.",
        " *",
        " * Both banks are static const tables in .const, each a palette of its",
        " * distinct words + an index byte per word; the code loads their",
        " * address with MVKL/MVKH, which the linker gives .rela.dyn entries so",
        " * the pedal fixes it when it places .const (as stock effects do). */",
        f"#define DB_NUM_BEATS {nb}",
        f"#define DB_BEAT_RANDOM {BEAT_RANDOM}   /* Randm: a new groove per section */",
        f"#define DB_BEAT_RANDOM_FIRST {BEAT_RANDOM_FIRST}",
        f"#define DB_BEAT_RANDOM_COUNT {BEAT_RANDOM_COUNT}",
        f"#define DB_BEAT_RNG_SEED 0x{BEAT_RNG_SEED:08X}u",
        f"#define DB_BEAT_WORDS {WORDS}",
    ] + _swing_lines() + [
        f"#define DB_NUM_FILLS {nf}",
        f"#define DB_FILL_RANDOM {FILL_RANDOM}   /* Randm: pick a fill per fill bar */",
        f"#define DB_FILL_RANDOM_COUNT {FILL_RANDOM_COUNT}   /* fills 2.. it picks from */",
        f"#define DB_FILL_RNG_SEED 0x{FILL_RNG_SEED:08X}u",
        f"#define DB_FILL_WORDS {1 + WORDS}",
        "#define DB_BEATS_TOTAL (DB_NUM_BEATS * DB_BEAT_WORDS)",
        "#define DB_FILLS_TOTAL (DB_NUM_FILLS * DB_FILL_WORDS)",
        "",
    ]
    # Beats as a palette of the distinct words plus one index byte per word
    # (DB_BEAT_WORD(i) = word i of the flat bank).
    pal: list[int] = []
    for idx in range(nb):
        for w in beat_words(idx):
            if w not in pal:
                pal.append(w)
    if len(pal) > 256:
        raise ValueError(f"{len(pal)} distinct beat words, the index is one byte")
    lines += [f"#define DB_BEAT_PAL_SIZE {len(pal)}",
              "static const uint32_t db_beat_pal[DB_BEAT_PAL_SIZE] = {"]
    for j in range(0, len(pal), 6):
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in pal[j:j + 6]) + ",")
    lines += ["};", "static const uint8_t db_beat_idx[DB_BEATS_TOTAL] = {"]
    for idx, (_name, short, _l) in enumerate(BEATS):
        words = ", ".join(f"{pal.index(w):3d}" for w in beat_words(idx))
        lines.append(f"    /* {idx:2d} {short:<5} */ {words},")
    lines += ["};", "#define DB_BEAT_WORD(i) (db_beat_pal[db_beat_idx[(i)]])", ""]
    # Fills the same way (DB_FILL_WORD(i) = word i of the flat bank).
    fpal: list[int] = []
    for idx in range(nf):
        for w in fill_words(idx):
            if w not in fpal:
                fpal.append(w)
    if len(fpal) > 256:
        raise ValueError(f"{len(fpal)} distinct fill words, the index is one byte")
    lines += [f"#define DB_FILL_PAL_SIZE {len(fpal)}",
              "static const uint32_t db_fill_pal[DB_FILL_PAL_SIZE] = {"]
    for j in range(0, len(fpal), 6):
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in fpal[j:j + 6]) + ",")
    lines += ["};", "static const uint8_t db_fill_idx[DB_FILLS_TOTAL] = {"]
    for idx, f in enumerate(FILLS):
        words = ", ".join(f"{fpal.index(w):3d}" for w in fill_words(idx))
        lines.append(f"    /* {idx:2d} {f[1]:<5} */ {words},")
    lines += ["};", "#define DB_FILL_WORD(i) (db_fill_pal[db_fill_idx[(i)]])", "",
              "/* Knob display: knob value -> screen name (see patterns.py). */"]
    lines += _getstr_table_lines("DB_BEAT_GETSTR_BODY", "db_beat_names", [b[1] for b in BEATS])
    lines += [""]
    lines += _getstr_table_lines("DB_FILL_GETSTR_BODY", "db_fill_names", [f[1] for f in FILLS])
    lines += ["", "/* Length knobs: knob value -> section length in bars | middle fill << 8,",
              " * and name. Arithmetic, not a compare chain (smaller code): entry 0, 1 = 0, 1 bars,",
              " * then 2^(v / 2) bars, odd entries with the middle fill. */",
              f"#define DB_NUM_LENGTHS {len(LENGTHS)}",
              "#define DB_LENGTH_FROM_KNOB(v) ( \\",
              "    (v) <= 1 ? (v) : \\",
              f"    (v) >= {len(LENGTHS) - 1} ? {LENGTHS[-1][1] | LENGTHS[-1][2] << 8} : \\",
              "    (1 << ((v) >> 1)) | ((v) & 1) << 8)"]
    lines += _getstr_table_lines("DB_LENGTH_GETSTR_BODY", "db_length_names", [n for n, _b, _m in LENGTHS])
    lines += ["", "#endif /* DRUMBOX_PATTERNS_H */", ""]
    Path(out_path).write_text("\n".join(lines))


if __name__ == "__main__":
    if "--export" in sys.argv:
        out = Path(sys.argv[sys.argv.index("--export") + 1])
        out.write_bytes(_PF.text().encode("utf-8"))
        print(f"{len(BEATS)} beats, {len(FILLS)} fills -> {out}")
    else:
        write_header(Path(__file__).resolve().parent / "drumbox_patterns.h")
        print(f"{len(BEATS)} beats, {len(FILLS)} fills")
