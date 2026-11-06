"""SongBox rhythm patterns: patterns.json is the source (made for an editor).

This module reads and checks patterns.json; proto.py and build.py use
PATTERNS, DRUM_STYLES and DEFAULTS from here.

patterns.json:
  {"format": "songbox-patterns", "version": 1,
   "defaults": {"Rhy1": <screen>, "Rhy2": <screen>, "Rhy3": <screen>},
   "patterns": [{"screen", "name", "bass", "keys", "drums"}, ...]}
  screen  1..5 characters, unique (the Rhy knob text)
  bass    {"notes": 16 steps, "len": 16 steps}: notes R root, 5 fifth,
          O octave, . none; len = the note's length in 16ths (hex 1..F)
          under each note, . elsewhere
  keys    the same with notes C chord, 1 / 2 / 3 one chord tone (arpeggio)
  drums   DrumBox screen names: {"beat", "mid" (middle fill, no crash),
          "end" (end fill)}

  python -B patterns/song_patterns.py  # check patterns.json, list it
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PATTERNS_JSON = Path(__file__).resolve().parent / "patterns.json"
FORMAT = "songbox-patterns"
STEPS = 16
NOTES = {"bass": "R5O", "keys": "C123"}         # char -> value = index (0..)
KNOBS = ("Rhy1", "Rhy2", "Rhy3")


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
    """(patterns, drum_styles, defaults) from patterns.json, checked."""
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
        if not isinstance(dr, dict) or set(dr) != {"beat", "mid", "end"}:
            raise PatternError(f"{where}: drums must be {{\"beat\", \"mid\", \"end\"}}")
        pats.append(dict(name=p.get("name", scr), short=scr,
                         bass=_lane(where, "bass", p.get("bass")),
                         keys=_lane(where, "keys", p.get("keys"))))
        styles[scr] = (dr["beat"], dr["mid"], dr["end"])
    if not pats:
        raise PatternError(f"{path.name}: no patterns")
    if len(pats) > 101:
        raise PatternError(f"{path.name}: at most 101 patterns (Rhy knobs 0..100)")
    dflt = data.get("defaults") or {}
    defaults = {}
    for k in KNOBS:
        if dflt.get(k) not in seen:
            raise PatternError(f"defaults: {k} must name a pattern's screen")
        defaults[k] = [p["short"] for p in pats].index(dflt[k])
    return pats, styles, defaults


PATTERNS, DRUM_STYLES, DEFAULTS = load()


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    for i, p in enumerate(PATTERNS):
        print(f"{i:3d} {p['short']:5s} {p['name']:10s} drums {'/'.join(DRUM_STYLES[p['short']])}")
    print("defaults", ", ".join(f"{k} {PATTERNS[v]['short']}" for k, v in DEFAULTS.items()))
