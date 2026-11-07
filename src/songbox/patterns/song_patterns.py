"""SongBox rhythm patterns: patterns.json is the source (made for an editor).

This module reads and checks patterns.json; proto.py and build.py use
PATTERNS, DRUM_STYLES and DEFAULTS from here.

patterns.json:
  {"format": "songbox-patterns", "version": 1,
   "defaults": {"Rhy1": <screen>, "Rhy2": <screen>, "Rhy3": <screen>},
   "patterns": [{"screen", "name", "bass", "keys", "drums"}, ...]}
  screen  1..5 characters, unique (the Rhy knob text)
  bass    {"notes": 16 steps, "len": 16 steps}: one character per note
          (below), . none; len = the note's length in 16ths (hex 1..F)
          under each note, . elsewhere
  keys    the same with the keys characters
Notes are relative to the bar's chord (songs store root + major / minor
only, so a pattern's color goes onto every chord it plays); minor chords
take the minor 3rd and b7:
  bass  R root, 5 fifth, O octave, 3 third, 6 major 6th (also on minor:
        Dorian), b b7, 7 major 7th (b7 on minor), 2 second (9th),
        A approach: a half step below the next song bar's root (below its
        own root when the next bar has no chord)
  keys  C triad in the G3..F#4 window (the inversion that fits, so chords
        move little); 1 / 2 / 3 the low / mid / high voice of it
        (arpeggio); S sus4 R-4-5; T sus2 R-2-5; 9 single tone 9th
        (arpeggio). Counted up from the root at C3..B3 (not folded, always
        chord tones): chords P root position R-3-5, I 1st inversion 3-5-R,
        J 2nd inversion 5-R-3, W wide R-5-10; single tones (arpeggio)
        R root, 5 fifth, O octave, E 3rd above the octave (10th), e.g.
        R.5.O.5.E.5.O.5.
A note outside both the chord and the song's key (the packer's key as a
major scale, + the leading tone when the first chord is the key's vi, as
Am in C) is replaced: bass 6 / b / 7 / 2 by the root, keys S / T by the
triad, keys 9 by the triad's low voice (songbox.c ss_key_mask).
  drums   DrumBox screen names: {"beat", "mid" (middle fill, no crash),
          "end" (end fill), optional "bar2" (fill played on every second
          drum bar of a run: a 2-bar beat as beat + fill, no crash)}
  norandom  optional true: Randm never picks it; these come last
The swing comes from the DrumBox beat. Rhy knob 0 = Randm (a style per run
of bars, from the styles without norandom), 1..N = the patterns.

  python -B patterns/song_patterns.py  # check patterns.json, list it
  python -B patterns/song_patterns.py --readme   # rewrite README.md's pattern table
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PATTERNS_JSON = Path(__file__).resolve().parent / "patterns.json"
FORMAT = "songbox-patterns"
STEPS = 16
NOTES = {"bass": "R5O36b72A", "keys": "C123ST9PIJWR5OE"}   # char -> code = index (0..)
# Semitones above the root per code, [major, minor] (bass code 8 = approach:
# -1 from the NEXT bar's root); keys: three tones per code [major 3, minor 3]
# (codes below KEY_ROOTED folded into one octave by the player, then sorted;
# from KEY_ROOTED on above the root at 48 + root, C3..B3, not folded).
BASS_OFS = [(0, 0), (7, 7), (12, 12), (4, 3), (9, 9), (10, 10), (11, 10), (2, 2), (-1, -1)]
BASS_APPROACH = 8
KEY_OFS = [((0, 4, 7), (0, 3, 7))] * 4 + [
    ((0, 5, 7), (0, 5, 7)),                 # S
    ((0, 2, 7), (0, 2, 7)),                 # T
    ((2, 2, 2), (2, 2, 2)),                 # 9
    ((0, 4, 7), (0, 3, 7)),                 # P  root position  (rooted from here)
    ((4, 7, 12), (3, 7, 12)),               # I  1st inversion
    ((7, 12, 16), (7, 12, 15)),             # J  2nd inversion
    ((0, 7, 16), (0, 7, 15)),               # W  wide R-5-10
    ((0, 0, 0), (0, 0, 0)),                 # R
    ((7, 7, 7), (7, 7, 7)),                 # 5
    ((12, 12, 12), (12, 12, 12)),           # O
    ((16, 16, 16), (15, 15, 15)),           # E
]
KEY_CHORDS = {0, 4, 5, 7, 8, 9, 10}         # all three voices; the others one voice in turn
KEY_SINGLE = 6                              # key-checked codes S, T, 9: single tones from here
KEY_ROOTED = 7                              # keys codes from here: above the root, unfolded (all chord tones)
assert len(BASS_OFS) == len(NOTES["bass"]) and len(KEY_OFS) == len(NOTES["keys"])
KNOBS = ("Rhy1", "Rhy2", "Rhy3")
MAX_PATTERNS = 149                              # + Randm = 150 knob values (genre beats + variations)


class PatternError(ValueError):
    pass


def _lane(where: str, lane: str, d) -> list:
    """{"notes", "len"} -> [(step, len, value)]."""
    if not isinstance(d, dict) or set(d) != {"notes", "len"}:
        raise PatternError(f"{where}: {lane} must be {{\"notes\": ..., \"len\": ...}}")
    notes, lens = d["notes"], d["len"]
    for k, s in (("notes", notes), ("len", lens)):
        if not isinstance(s, str) or len(s) != STEPS:
            raise PatternError(f"{where}: {lane}.{k} must be {STEPS} characters")
    out = []
    for step, (n, ln) in enumerate(zip(notes, lens)):
        if n == ".":
            if ln != ".":
                raise PatternError(f"{where}: {lane} step {step + 1}: a length without a note")
            continue
        if n not in NOTES[lane]:
            raise PatternError(f"{where}: {lane} step {step + 1}: note {n!r} is not one of "
                               f"{', '.join(NOTES[lane])} or .")
        try:
            length = int(ln, 16)
        except ValueError:
            length = 0
        if not 1 <= length <= 15:
            raise PatternError(f"{where}: {lane} step {step + 1}: length {ln!r} is not 1..F")
        out.append((step, length, NOTES[lane].index(n)))
    return out


def load(path: Path = PATTERNS_JSON):
    """(patterns, drum_styles, defaults) from patterns.json, checked;
    defaults are knob values (pattern index + 1)."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise PatternError(f"{path.name} line {e.lineno}: {e.msg}") from None
    if data.get("format") != FORMAT or data.get("version") != 1:
        raise PatternError(f"{path.name}: not a {FORMAT} version 1 file")
    pats, styles, seen = [], {}, set()
    for i, p in enumerate(data.get("patterns") or []):
        where = f"pattern {i + 1} ({p.get('screen', '?')})"
        scr = p.get("screen")
        if not isinstance(scr, str) or not 1 <= len(scr) <= 5 or not scr.isascii():
            raise PatternError(f"{where}: screen must be 1..5 ASCII characters")
        if scr in seen:
            raise PatternError(f"{where}: screen name used twice")
        seen.add(scr)
        dr = p.get("drums")
        if (not isinstance(dr, dict) or not {"beat", "mid", "end"} <= set(dr)
                or not set(dr) <= {"beat", "mid", "end", "bar2"}):
            raise PatternError(f"{where}: drums must be {{\"beat\", \"mid\", \"end\"[, \"bar2\"]}}")
        nr = p.get("norandom", False)
        if nr not in (False, True):
            raise PatternError(f"{where}: norandom must be true or false")
        if pats and pats[-1]["norandom"] and not nr:
            raise PatternError(f"{where}: norandom patterns must come last")
        pats.append(dict(name=p.get("name", scr), short=scr, norandom=nr,
                         bass=_lane(where, "bass", p.get("bass")),
                         keys=_lane(where, "keys", p.get("keys"))))
        styles[scr] = (dr["beat"], dr["mid"], dr["end"], dr.get("bar2"))
    if not pats:
        raise PatternError(f"{path.name}: no patterns")
    if len(pats) > MAX_PATTERNS:
        raise PatternError(f"{path.name}: at most {MAX_PATTERNS} patterns (Rhy knobs 0..{MAX_PATTERNS}, 0 = Randm)")
    if all(p["norandom"] for p in pats):
        raise PatternError(f"{path.name}: Randm needs a pattern without norandom")
    dflt = data.get("defaults") or {}
    defaults = {}
    for k in KNOBS:
        if dflt.get(k) not in seen:
            raise PatternError(f"defaults: {k} must name a pattern's screen")
        defaults[k] = [p["short"] for p in pats].index(dflt[k]) + 1     # knob value (0 = Randm)
    return pats, styles, defaults


PATTERNS, DRUM_STYLES, DEFAULTS = load()


def readme_table() -> list:
    """README.md's pattern table: one row per Rhy knob value (Randm, then
    the patterns), swing from the DrumBox beat (as DrumBox's README)."""
    db = json.loads((PATTERNS_JSON.parent.parent.parent / "drumbox" / "patterns" / "patterns.json")
                    .read_text(encoding="utf-8"))
    swing = {e["screen"]: e.get("swing", "") for g in db["beats"] for e in g["entries"]}
    rows = ["| Knob | Screen | Pattern | Swing |", "|---|---|---|---|",
            "| 0 | Randm | Random (a pattern for each run of bars with this rhythm, "
            "picked again every time the song comes to it) |  |"]
    for i, p in enumerate(PATTERNS):
        rows.append(f"| {i + 1} | {p['short']} | {p['name']} | {swing[DRUM_STYLES[p['short']][0]]} |")
    return rows


def write_readme(path: Path = PATTERNS_JSON.parent.parent / "README.md") -> None:
    """Replace the table after "## Patterns" (CRLF kept)."""
    raw = path.read_bytes().decode("utf-8")
    nl = "\r\n" if "\r\n" in raw else "\n"
    lines = raw.split(nl)
    h = lines.index("## Patterns")
    a = next(i for i in range(h, len(lines)) if lines[i].startswith("|"))
    b = next(i for i in range(a, len(lines)) if not lines[i].startswith("|"))
    lines[a:b] = readme_table()
    path.write_bytes(nl.join(lines).encode("utf-8"))
    print(f"{path.name}: {len(PATTERNS)} patterns")


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    if "--readme" in sys.argv:
        write_readme()
        sys.exit(0)
    for i, p in enumerate(PATTERNS):
        dr = "/".join(d for d in DRUM_STYLES[p["short"]] if d)
        print(f"{i + 1:3d} {p['short']:5s} {p['name']:16s} drums {dr}{'  norandom' if p['norandom'] else ''}")
    print("defaults", ", ".join(f"{k} {PATTERNS[v - 1]['short']}" for k, v in DEFAULTS.items()))
