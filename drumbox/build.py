#!/usr/bin/env python3
"""Build DrumBox.ZDL from drumbox.c + manifest.json + patterns.py + kits.py.

Uses the linker in ../build/ (based on repeat98's).

TI compiler lookup order:
  1. TI_CGT_ROOT environment variable
  2. common Windows install locations (C:/ti/...)

Usage:
  python -B build.py            # full build -> ../dist/DrumBox.ZDL
  python -B build.py --nop      # smoke build: UI only, DSP replaced by NOP
  python -B build.py --headers  # only regenerate headers (no TI needed)
  python -B build.py --no-init  # without the materializing _init (bisect only)
  python -B build.py --out P    # write the ZDL to P instead
  python -B build.py --gid 7 --out P   # another category (7 = SFX), for testing
  python -B build.py --probe --out P   # load probe: cycle meter + beep report at Volume 0 (DB_PROBE)
  ZDL_MATERIALIZE_INIT_MAX_CALLS=0 python -B build.py   # init frame + ctx[34] only (bisect)
"""

from __future__ import annotations

import glob
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent                                   # Custom-Zoom-ZDL/
sys.path.insert(0, str(PROJECT / "build"))            # linker, screen_image, manifest_params

if "--probe" in sys.argv:
    # room for the meter under the load limit: Ac2 and Ac3
    # leave the probe build; set before kits.py is imported
    os.environ.setdefault("DRUMBOX_DROP_KITS", "Ac2 Ac8")

from manifest_params import write_param_header  # noqa: E402
from patterns import write_header as write_pattern_header  # noqa: E402
from kits import write_header as write_kit_header  # noqa: E402
from screen_image import encode_zoom_rle  # noqa: E402

# 8.5.0.LTS (bundled with CCS 20.x) is the hardware-proven version; the
# standalone 8.3.x installer is accepted as a fallback.
TI_CANDIDATES = [
    "C:/ti/ti-cgt-c6000_8.5.0.LTS",
    "C:/ti/ccs*/ccs/tools/compiler/ti-cgt-c6000_8.5.0.LTS",
    "C:/ti/ccs*/tools/compiler/ti-cgt-c6000_8.5.0.LTS",
    "C:/ti/ti-cgt-c6000_*",
    "C:/ti/ccs*/ccs/tools/compiler/ti-cgt-c6000_*",
]


def find_ti_root() -> Path:
    env = os.environ.get("TI_CGT_ROOT")
    if env and (Path(env) / "bin").is_dir():
        return Path(env)
    for pat in TI_CANDIDATES:
        for hit in sorted(glob.glob(pat)):
            if (Path(hit) / "bin").is_dir():
                return Path(hit)
    sys.exit("[drumbox] TI C6000 CGT 8.5.0.LTS not found. Install it or set TI_CGT_ROOT.")


from screen import KNOB_SLOTS  # noqa: E402


def make_screen() -> bytes:
    """128x64 bitmap from screen.py: bold DRUM / BOX title and a drum kit."""
    from screen import draw
    return encode_zoom_rle(draw())


def main() -> None:
    # The linker prints arrows etc.; a cp1251/cp1252 console would crash on them.
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    args = set(sys.argv[1:])
    manifest = json.loads((HERE / "manifest.json").read_text())
    # The Kit knob lists every kit in kits.py exactly once: its max follows
    # the kit count, so adding or removing a kit needs no manifest edit.
    from kits import KITS, KNOB_MAX
    # Beat and fill knobs likewise follow patterns.py (one step per entry),
    # with their defaults given there by screen name.
    import patterns as pt
    knob_lists = {
        "BeatA": (len(pt.BEATS) - 1, pt.beat_index(pt.DEFAULT_BEAT_A)),
        "FillA": (len(pt.FILLS) - 1, pt.fill_index(pt.DEFAULT_FILL_A)),
        "LenA": (len(pt.LENGTHS) - 1, pt.length_index(pt.DEFAULT_LEN_A)),
        "BeatB": (len(pt.BEATS) - 1, pt.beat_index(pt.DEFAULT_BEAT_B)),
        "FillB": (len(pt.FILLS) - 1, pt.fill_index(pt.DEFAULT_FILL_B)),
        "LenB": (len(pt.LENGTHS) - 1, pt.length_index(pt.DEFAULT_LEN_B)),
    }
    for prm in manifest["params"]:
        if prm["name"] == "Kit":
            prm["max"] = KNOB_MAX
            prm["default"] = min(prm["default"], KNOB_MAX)
            print(f"[drumbox] Kit knob: {len(KITS)} kits, max {KNOB_MAX}")
        elif prm["name"] in knob_lists:
            prm["max"], prm["default"] = knob_lists[prm["name"]]
            prm["audio_default"] = prm["default"] / 100.0     # raw = knob / 100
            print(f"[drumbox] {prm['name']} knob: max {prm['max']}, default {prm['default']}")
    write_param_header(manifest, HERE / "drumbox_params.h", "DRUMBOX")
    write_pattern_header(HERE / "drumbox_patterns.h")
    write_kit_header(HERE / "drumbox_kits.h")
    print("[drumbox] headers regenerated")
    if "--headers" in args:
        return

    from linker import LinkerConfig, link, params_from_manifest  # noqa: E402

    ti_root = find_ti_root()
    cl6x = ti_root / "bin" / "cl6x"
    cflags = [
        "--c99",
        "--opt_level=2",
        "--opt_for_space=3",               # -8.6 KB of code vs none
        "--disable_push_pop",              # else -ms calls __c6xabi_push_rts/pop_rts (not in the Zoom runtime)
        "-mv6740",
        "--abi=eabi",
        "--mem_model:data=far",            # critical: no B14-relative statics
        f"--include_path={ti_root}/include",
    ]

    if "--probe" in args:
        if "--out" not in sys.argv:
            sys.exit("[drumbox] --probe needs --out (probes never go to dist/)")
        cflags.append("--define=DB_PROBE=1")
    if "--gid" in sys.argv:
        # another category: the SONAME prefix and the function names follow it
        from linker import GID_PREFIX
        manifest["gid"] = int(sys.argv[sys.argv.index("--gid") + 1])
        manifest["audio_func_name"] = f"Fx_{GID_PREFIX[manifest['gid']]}_{manifest['effect_name']}"
        cflags.append(f"--define=DB_AUDIO_FUNC={manifest['audio_func_name']}")

    src_c = HERE / "drumbox.c"
    obj = HERE / "drumbox.obj"
    out_zdl = PROJECT / "dist" / f"{manifest['effect_name']}.ZDL"
    if "--out" in sys.argv:
        out_zdl = Path(sys.argv[sys.argv.index("--out") + 1]).resolve()
    out_zdl.parent.mkdir(exist_ok=True)

    print(f"[drumbox] compiling {src_c.name} -> {obj.name} ({ti_root})")
    subprocess.run([str(cl6x), *cflags, "-c", str(src_c), f"--output_file={obj}"],
                   check=True, cwd=HERE)
    for junk in ("compiler.opt", "linker.cmd"):
        p = HERE / junk
        if p.exists():
            p.unlink()

    cfg = LinkerConfig(
        effect_name=manifest["effect_name"],
        audio_func_name=manifest.get("audio_func_name"),
        gid=manifest["gid"],
        fxid=manifest["fxid"],
        params=params_from_manifest(manifest["params"]),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=manifest.get("fxid_version", "1.00").encode("ascii"),
        flags_byte=manifest.get("flags_byte", 0x01),
        screen_image=make_screen(),
        # No knobs on the screen image: an own 1x1 knob graphic (11 blank
        # frames, like stock Ice Delay's own 20x15 knob) placed on pixels the
        # image leaves black (screen.KNOB_SLOTS), so the firmware's knobs vanish.
        knob_positions=[(i + 2, x, y) for i, (x, y) in enumerate(KNOB_SLOTS)],
        knob_bitmap=(1, 1, 11, bytes(11)),
        audio_nop=manifest.get("audio_nop", False) or "--nop" in args,
        use_object_edit_handlers=False,
        synthesize_linesel_edit_handlers=True,
        synth_edit_start_index=2,
        knob3_blob_path=str(HERE / "__nonexistent__"),
        # Materializing _init: ctx[34](params, zeros, 56), then every knob's
        # edit handler, so saved values reach params[] on add / patch load /
        # power-up. Without the ctx[34] call it froze the G1Xon. --no-init
        # builds without it (then params[] keep stale values: for bisecting).
        materialize_init="--no-init" not in args,
        init_param_setup=True,
        # No float divide in the code: leave the 640-byte divf RTS out
        # (-640 B, room under the G1Xon load limit).
        drop_unused_divf=True,
        # Cloned knob 3-9 handlers = 96 B each, sharing LineSel's call_stub
        # (-2,688 B).
        compact_linesel_clones=True,
        # LineSel blob without its unused _init, Dll and push/pop_rts
        # (-160 B).
        trim_linesel_blob=True,
        # DSP cost the pedal adds up per patch (budget 210 on the G1Xon):
        # measured ~15 % of the DSP while playing, ~35 % for some seconds
        # after a kit change -> 60.
        dsp_cost=manifest.get("dsp_cost", 20.0),
    )
    link(cfg)
    print(f"\n[drumbox] done -> {out_zdl}")


if __name__ == "__main__":
    main()
