"""Build SndBank.ZDL: the sound source for MelodyBox (SFX 531, in development).

  python -B src/sndbank/build.py --out P    # P = scratchpad until the first release

SndBank plays the notes MelodyBox sends over the MIDI bus
(src/common/midibus/midibus.h, channels 1 and 2) with SongBox's instrument
engine, one voice per channel; voice settings as in
src/common/notebus/notebus.h. Its 9
knobs hold stored data at the Floppy field widths and are locked (own edit
handler). See sndbank.c. The screen image is screen.png (128x64; pixels
darker than mid-grey are dark).
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                            # Custom-Zoom-ZDL/
sys.path.insert(0, str(PROJECT / "build"))            # linker, screen_image, zdl_build
sys.path.insert(0, str(PROJECT / "src" / "common" / "zmenu"))    # zmenu_gen (field widths)

from screen_image import Canvas  # noqa: E402
from zdl_build import compile_c, encode_rle, link_common  # noqa: E402
from zmenu_gen import FLOPPY_MAXES  # noqa: E402

FXID = 531
ZMENU = PROJECT / "src" / "common" / "zmenu"
NOTEBUS = PROJECT / "src" / "common" / "notebus"
MIDIBUS = PROJECT / "src" / "common" / "midibus"
FXINFO = PROJECT / "src" / "common" / "fxinfo"
SONGBOX = PROJECT / "src" / "songbox"                 # songbox_instr.h, instruments/
SPACE = 3                                               # --opt_for_space, as SongBox (the engine's loops)
DSP_COST = 24.0                                         # estimate: two SongBox voices (SongBox: 4 voices + player = 52); not measured
KNOB_SLOTS = [(1, 0), (126, 0), (127, 0)]                # blank 1x1 knobs, outside the frame
SCREEN = HERE / "screen.png"                              # 128x64, pixels darker than mid-grey = dark


def make_screen() -> bytes:
    from PIL import Image
    img = Image.open(SCREEN).convert("L")
    assert img.size == (128, 64), f"{SCREEN.name} is {img.size}, need 128x64"
    c = Canvas()
    for y in range(64):
        for x in range(128):
            c.px(x, y, 1 if img.getpixel((x, y)) <= 127 else 0)
    for x, y in KNOB_SLOTS:
        assert not any(c.pixels[y + z][x] for z in range(8)), f"knob slot {x},{y} not light"
    return encode_rle(c)


def build(out_zdl: Path) -> None:
    from linker import LinkerConfig, link, params_from_manifest  # noqa: E402

    # names Data1..Data9, texts "DON'T TOUCH THESE" on each page
    params = [{"name": f"Data{k + 1}", "type": "knob", "max": (1 << w) - 1, "default": 0, "store": True,
               "scale": "enum", "audio_min": 0.0, "audio_max": 100.0, "audio_default": 0.0,
               "edit": "Sb_LockEdit", "getstr": f"GetString_SbData{k % 3 + 1}"}
              for k, w in enumerate(FLOPPY_MAXES)]
    obj = HERE / "sndbank.obj"
    compile_c(HERE / "sndbank.c", obj, include=[ZMENU, NOTEBUS, MIDIBUS, FXINFO, SONGBOX], space=SPACE)
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
        screen_image=make_screen(),
        # no firmware knobs on the picture: own blank 1x1 knob graphic (11 frames)
        knob_positions=[(i + 2, x, y) for i, (x, y) in enumerate(KNOB_SLOTS)],
        knob_bitmap=(1, 1, 11, bytes(11)),
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
