"""Render the atom library (instruments/atoms.py) for listening.

    python -B atom_grid.py               every candidate
    python -B atom_grid.py named         the named emulations only
    python -B atom_grid.py grid          the atom x behaviour grid only
    python -B atom_grid.py NAME ...      these (named_<x>, grid_<beh>_<atom>)

-> preview/atoms/<name>.wav + index.txt. Keys candidates play a 4-bar
phrase alone (Am held chord, F stabs, C arpeggio, G two chords), basses a
4-bar line (held, quarters, eighths, root-fifth-octave); 1.2 s tail. Each
file is scaled to the same loudness (RMS), peak <= 0.9; index.txt lists per
candidate the table terms (pedal: ~4 B a term, + ~50 B record, + ~20 B kit).
"""
from __future__ import annotations

import math
import struct
import sys
import types
import wave
from multiprocessing import Pool
from pathlib import Path

HERE = Path(__file__).resolve().parent
for _d in (HERE, HERE / "instruments"):
    if str(_d) not in sys.path:
        sys.path.insert(0, str(_d))
from atoms import candidate, names  # noqa: E402
from engine import Instrument  # noqa: E402
from inst_lib import SR, Voice  # noqa: E402

BPM = 100
STEP = int(SR * 60 / BPM / 4 + 0.5)            # samples per 16th
TAIL = int(SR * 1.2)
OUT = HERE / "preview" / "atoms"
FREQ = [440.0 * 2.0 ** ((m - 69) / 12.0) for m in range(128)]

# (step, MIDI notes, length in steps); 3 notes = a chord on the 3 voices,
# 1 note = the next voice in turn (the others ring out)
KEYS = ([(0, (57, 60, 64), 15)]                                      # Am held
        + [(16 + s, (57, 60, 65), 2) for s in (0, 4, 8, 10, 12)]     # F stabs
        + [(32 + 2 * i, (n,), 3) for i, n in enumerate((60, 64, 67, 72, 67, 64, 60, 64))]  # C arpeggio
        + [(48, (55, 59, 62), 7), (56, (55, 59, 62), 8)])            # G
BASS = ([(0, (33,), 15)]                                             # A1 held
        + [(16 + s, (29,), 3) for s in (0, 4, 8, 12)]                # F1 quarters
        + [(32 + 2 * i, (36 if i % 2 == 0 else 48,), 2) for i in range(8)]  # C2 / C3 eighths
        + [(48 + 4 * i, (n,), 3) for i, n in enumerate((31, 38, 43, 38))])  # G1 D2 G2 D2
NSTEPS = 64


def play(inst, events) -> list:
    voices = [Voice() for _ in range(3)]
    at = {}
    for st, notes, ln in events:
        at.setdefault(st, []).append((notes, ln))
    out, rr = [], 0
    for step in range(NSTEPS + (TAIL + STEP - 1) // STEP):
        for notes, ln in at.get(step, ()):
            if len(notes) == 3:
                for v, n in zip(voices, notes):
                    v.trigger(FREQ[n], ln * STEP - STEP // 4)
            else:
                voices[rr % 3].trigger(FREQ[notes[0]], ln * STEP - STEP // 4)
                rr += 1
        for _ in range(STEP):
            s = 0.0
            for v in voices:
                s += inst.render(v)
                v.gate -= 1
            out.append(s)
    return out


def job(name: str) -> str:
    role, low, high, rec = candidate(name)
    mod = types.SimpleNamespace(__name__=name, LOW=low, HIGH=high, REC=rec)
    x = play(Instrument(mod), BASS if role == "bass" else KEYS)
    rms = math.sqrt(sum(s * s for s in x) / len(x)) or 1.0
    peak = max(abs(s) for s in x) or 1.0
    g = min(0.12 / rms, 0.9 / peak)
    with wave.open(str(OUT / f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * g)) * 32767)) for s in x))
    return f"{name:28s} {role:4s} terms {len(low):2d} + {len(high):2d}  peak {peak:.3f}  gain {g:.2f}"


def main() -> None:
    every = names()
    want = sys.argv[1:]
    if want == ["named"] or want == ["grid"]:
        sel = [n for n in every if n.startswith(want[0])]
    elif want:
        bad = [n for n in want if n not in every]
        if bad:
            sys.exit(f"unknown: {' '.join(bad)}")
        sel = want
    else:
        sel = every
    OUT.mkdir(parents=True, exist_ok=True)
    lines = []
    with Pool() as pool:
        for line in pool.imap(job, sel):
            print(line, flush=True)
            lines.append(line)
    if not want:
        (OUT / "index.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
