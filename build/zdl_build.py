"""Shared build helpers for the effects in src/ (SongBox, Floppy, the probes):
find the TI compiler, compile one .c, encode a screen picture, and the linker
options the zmenu effects share. DrumBox keeps its own copies (release build).
"""

from __future__ import annotations

import glob
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent                    # Custom-Zoom-ZDL/build/


def find_ti_root() -> Path:
    """TI_CGT_ROOT, then C:/ti installs (as src/drumbox/build.py)."""
    env = os.environ.get("TI_CGT_ROOT")
    if env and (Path(env) / "bin").is_dir():
        return Path(env)
    for pat in ("C:/ti/ti-cgt-c6000_8.5.0.LTS", "C:/ti/ccs*/ccs/tools/compiler/ti-cgt-c6000_8.5.0.LTS",
                "C:/ti/ti-cgt-c6000_*", "C:/ti/ccs*/ccs/tools/compiler/ti-cgt-c6000_*"):
        for hit in sorted(glob.glob(pat)):
            if (Path(hit) / "bin").is_dir():
                return Path(hit)
    sys.exit("[build] TI C6000 CGT not found. Install it or set TI_CGT_ROOT.")


def encode_rle(c) -> bytes:
    """Zoom RLE with runs capped at 257 (as G1XPeek's build.py)."""
    raw = [sum(1 << z for z in range(8) if c.pixels[yb * 8 + z][x])
           for yb in range(8) for x in range(c.W)]
    out: list[int] = []
    i = 0
    while i < len(raw):
        j = i + 1
        while j < len(raw) and raw[j] == raw[i] and j - i < 257:
            j += 1
        out += [raw[i]] if j - i == 1 else [raw[i], raw[i], j - i - 2]
        i = j
    return bytes(out)


def compile_c(src: Path, obj: Path, include: list[Path] = (), defines: list[str] = (),
              space: int = 1) -> None:
    ti = find_ti_root()
    cflags = ["--c99", "--opt_level=2", f"--opt_for_space={space}", "--disable_push_pop",
              "-mv6740", "--abi=eabi", "--mem_model:data=far", f"--include_path={ti}/include",
              *[f"--include_path={p}" for p in include], *[f"--define={d}" for d in defines]]
    subprocess.run([str(ti / "bin" / "cl6x"), *cflags, "-c", str(src), f"--output_file={obj}"],
                   check=True, cwd=obj.parent)
    for junk in ("compiler.opt", "linker.cmd"):
        (obj.parent / junk).unlink(missing_ok=True)


def link_common() -> dict:
    """Linker options for the zmenu effects (ctx[34] _init, trimmed blobs)."""
    return dict(
        use_object_edit_handlers=False,
        synthesize_linesel_edit_handlers=True,
        synth_edit_start_index=2,
        knob3_blob_path=str(HERE / "__nonexistent__"),
        materialize_init=True,
        init_param_setup=True,
        drop_unused_divf=True,
        compact_linesel_clones=True,
        trim_linesel_blob=True,
    )
