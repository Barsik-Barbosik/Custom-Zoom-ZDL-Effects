"""Build FLOPPY.ZDL: the storage companion for zmenu.h menus (SFX + 522).

Floppy is a plain effect with 9 knobs at the patch field maxes and no sound;
zmenu.h (src/common/zmenu/) in another effect finds it in the chain and packs that
effect's values into its knob columns, so they are saved in the patch.

  python -B build.py --out P              # FLOPPY.ZDL to P
  python -B build.py --knob-text --out P  # knob pages show the raw values (2 blocks)

build_floppy(maxes=...) builds test Floppies with other knob maxes.
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                            # Custom-Zoom-ZDL/
sys.path.insert(0, str(PROJECT / "build"))            # linker, screen_image, zdl_build
sys.path.insert(0, str(PROJECT / "src" / "common" / "zmenu"))    # zmenu_gen (field widths, fxid)
sys.path.insert(0, str(HERE))                          # screen.py

from zdl_build import compile_c, encode_rle, link_common  # noqa: E402
from zmenu_gen import FLOPPY_FXID, FLOPPY_MAXES  # noqa: E402


def build_floppy(out_zdl: Path, maxes: tuple[int, ...] | None = None, defines: list[str] = (),
                 knob_text: bool = False) -> None:
    """Floppy: 9 storage knobs at the G1on field maxes, sound untouched.
    `maxes` / `defines`: other knob maxes for field-width tests.
    knob_text: the knob pages show the raw column values (GetString_FlNum,
    ~350 B more: 2 flash blocks instead of 1)."""
    from linker import LinkerConfig, link, params_from_manifest  # noqa: E402
    from screen import KNOB_SLOTS, draw  # noqa: E402

    params = [{"name": f"Data{i + 1}", "type": "knob", "max": mx, "default": 0,
               "store": True, "scale": "enum", "audio_min": 0.0, "audio_max": 100.0,
               "audio_default": 0.0, **({"getstr": "GetString_FlNum"} if knob_text else {})}
              for i, mx in enumerate(maxes or [(1 << w) - 1 for w in FLOPPY_MAXES])]
    obj = HERE / "floppy.obj"
    if knob_text:
        defines = [*defines, "FL_KNOB_TEXT"]
    compile_c(HERE / "floppy.c", obj, defines=sorted(set(defines)))
    link(LinkerConfig(
        effect_name="Floppy",
        audio_func_name="Fx_SFX_Floppy",
        gid=7,
        fxid=FLOPPY_FXID,
        params=params_from_manifest(params),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=b"1.00",
        flags_byte=1,
        screen_image=encode_rle(draw()),
        # Floppy disk picture without knobs: an own 1x1 knob graphic (11 blank
        # frames) at slots the picture leaves light, as DrumBox does.
        knob_positions=[(i + 2, x, y) for i, (x, y) in enumerate(KNOB_SLOTS)],
        knob_bitmap=(1, 1, 11, bytes(11)),
        dsp_cost=2.0,
        # 1 flash block (4,090 B): Floppy never reads params[] (zmenu reads
        # its patch columns), so no _init; its 9 knob handlers are 16-byte
        # stubs into one shared body; no TI bookkeeping symbols, .dynamic
        # without its DT_NULL padding; as SongBox: no .c6xabi.attributes,
        # knob handler dynsyms without names.
        **{**link_common(), "materialize_init": False, "init_param_setup": False,
           "synth_edit_start_index": 0, "compact_edit_stubs": True,
           "drop_ti_abs_symbols": True, "dynamic_entries": 17,
           "drop_attributes": True, "anon_edit_syms": True},
    ))
    obj.unlink(missing_ok=True)


def main() -> None:
    args = sys.argv[1:]
    knob_text = "--knob-text" in args
    if knob_text:
        args.remove("--knob-text")
    if len(args) != 2 or args[0] != "--out":
        sys.exit("usage: python -B build.py [--knob-text] --out P")
    build_floppy(Path(args[1]).resolve(), knob_text=knob_text)


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    main()
