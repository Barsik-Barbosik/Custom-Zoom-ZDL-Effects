"""Build SndBank.ZDL: the sound source for Melody Box (SFX 528, in development).

  python -B src/sndbank/build.py --out P    # P = scratchpad until the first release

SndBank plays the notes Melody Box sends over the note bus
(src/common/notebus/notebus.h) with SongBox's instrument engine. Its 9
knobs hold stored data at the Floppy field widths and are locked (own edit
handler). See sndbank.c.
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                            # Custom-Zoom-ZDL/
sys.path.insert(0, str(PROJECT / "build"))            # linker, screen_image, zdl_build
sys.path.insert(0, str(PROJECT / "src" / "common" / "zmenu"))    # zmenu_gen (field widths)

from zdl_build import compile_c, link_common  # noqa: E402
from zmenu_gen import FLOPPY_MAXES  # noqa: E402

FXID = 528
ZMENU = PROJECT / "src" / "common" / "zmenu"
NOTEBUS = PROJECT / "src" / "common" / "notebus"
SONGBOX = PROJECT / "src" / "songbox"                 # songbox_instr.h, instruments/
SPACE = 3                                               # --opt_for_space, as SongBox (the engine's loops)
DSP_COST = 12.0                                         # estimate: one SongBox voice (SongBox: 4 voices + player = 52); not measured


def build(out_zdl: Path) -> None:
    from linker import LinkerConfig, link, params_from_manifest  # noqa: E402

    params = [{"name": "", "type": "knob", "max": (1 << w) - 1, "default": 0, "store": True,
               "scale": "enum", "audio_min": 0.0, "audio_max": 100.0, "audio_default": 0.0,
               "edit": "Sb_LockEdit", "getstr": "GetString_SbData"}
              for w in FLOPPY_MAXES]
    obj = HERE / "sndbank.obj"
    compile_c(HERE / "sndbank.c", obj, include=[ZMENU, NOTEBUS, SONGBOX], space=SPACE)
    link(LinkerConfig(
        effect_name="SndBank",
        audio_func_name="Fx_SFX_SndBank",
        gid=7,
        fxid=FXID,
        params=params_from_manifest(params),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=b"0.10",
        flags_byte=1,
        dsp_cost=DSP_COST,
        **link_common(),
    ))
    obj.unlink(missing_ok=True)


def main() -> None:
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--out":
        out = Path(args[1]).resolve()
    else:
        sys.exit("usage: python -B src/sndbank/build.py --out P")
    out.parent.mkdir(parents=True, exist_ok=True)
    build(out)


if __name__ == "__main__":
    for stream_ in (sys.stdout, sys.stderr):
        stream_.reconfigure(encoding="utf-8", errors="replace")
    main()
