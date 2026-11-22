"""Build MeloBox.ZDL: Melody Box, a note sequencer / player (SFX 525).

  python -B src/melodybox/build.py --out P    # P = scratchpad until the first release

Items "" (empty), steps 01..64, "END"; knobs 1-3 show items off .. off + 2
(start: "", 01___, 02___). A step's name = number + note ("01___", "01__C",
"01_A#"). Knob texts: knob 1 "NOTE" (does nothing yet), knob 2 its own name
(the note of its step, 0 = none, 1..12 = C .. B), knob 3 "SCROLL" (the
scroll offset 0..63). The audio callback renames the three knobs. See
melodybox.c.
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                            # Custom-Zoom-ZDL/
sys.path.insert(0, str(PROJECT / "build"))            # linker, screen_image, zdl_build

from zdl_build import compile_c, link_common  # noqa: E402

FXID = 525
MAX_OFF = 63                                            # mirrors MB_MAXOFF in melodybox.c
MAX_NOTE = 12                                           # mirrors MB_MAXNOTE in melodybox.c
ZMENU = PROJECT / "src" / "common" / "zmenu"


def build(out_zdl: Path) -> None:
    from linker import LinkerConfig, link, params_from_manifest  # noqa: E402

    def knob(name: str, mx: int, getstr: str) -> dict:
        return {"name": name, "type": "knob", "max": mx, "default": 0, "scale": "enum",
                "audio_min": 0.0, "audio_max": float(mx), "audio_default": 0.0,
                "getstr": getstr}

    params = [
        knob("", 100, "GetString_MbNote"),              # reserved; text "NOTE"
        knob("01___", MAX_NOTE, "GetString_MbItem"),    # note of its step; text = its name
        knob("02___", MAX_OFF, "GetString_MbScroll"),   # scroll; name rewritten at run time
    ]
    obj = HERE / "melodybox.obj"
    compile_c(HERE / "melodybox.c", obj, include=[ZMENU])
    link(LinkerConfig(
        effect_name="MeloBox",
        audio_func_name="Fx_SFX_MeloBox",
        gid=7,
        fxid=FXID,
        params=params_from_manifest(params),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=b"0.10",
        flags_byte=1,
        dsp_cost=1.0,
        **link_common(),
    ))
    obj.unlink(missing_ok=True)


def main() -> None:
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--out":
        out = Path(args[1]).resolve()
    else:
        sys.exit("usage: python -B src/melodybox/build.py --out P")
    out.parent.mkdir(parents=True, exist_ok=True)
    build(out)


if __name__ == "__main__":
    for stream_ in (sys.stdout, sys.stderr):
        stream_.reconfigure(encoding="utf-8", errors="replace")
    main()
