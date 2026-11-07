"""Precision bass prototypes for SongBox: the variants of
instruments/pbass_model.py rendered as WAVs to pick from.

    python -B pbass_proto.py      -> preview/pbass_NN_<name>.wav

Each WAV: the first 12 bars of proto.py's song, bass alone, then the same
bars with EPiano chords, 1 s tail.
"""
from __future__ import annotations

import sys
import types
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import proto  # noqa: E402
from sets import load  # noqa: E402
import engine  # noqa: E402  (sets put instruments/ on the path)
from pbass_model import VARIANTS  # noqa: E402

BARS = 12                       # the song's first 12 bars (rhythms 1 and 2)
GAIN = 1.0                      # common WAV gain (instrument LEVELs without MASTER)

_SILENT = types.SimpleNamespace(render=lambda v: 0.0, LEVEL=0.0)


def bass_track(i: int) -> list:
    mod = VARIANTS[i]
    ib = types.SimpleNamespace(render=engine.Instrument(mod).render, LEVEL=mod.LEVEL * GAIN)
    return proto.render_with(_SILENT, ib, proto.SONG[:BARS], keys_on=False)


def keys_track() -> list:
    mod = load("epiano")
    ik = types.SimpleNamespace(render=engine.Instrument(mod).render, LEVEL=mod.LEVEL * GAIN)
    return proto.render_with(ik, _SILENT, proto.SONG[:BARS])


def main() -> None:
    out = HERE / "preview"
    out.mkdir(exist_ok=True)
    n = BARS * 16 * proto.STEP                      # samples without the tail
    with ProcessPoolExecutor() as ex:
        kf = ex.submit(keys_track)
        tracks = list(ex.map(bass_track, range(len(VARIANTS))))
        keys = kf.result()
    for i, (mod, b) in enumerate(zip(VARIANTS, tracks)):
        mix = [max(-0.99, min(0.99, x + y)) for x, y in zip(b, keys)]
        print(f"{i + 1:2}: {mod.DOC}")
        proto.write_wav(out / f"pbass_{i + 1:02}_{mod.NAME}.wav", b[:n] + mix)


if __name__ == "__main__":
    main()
