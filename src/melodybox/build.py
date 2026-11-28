"""Build MeloBox.ZDL: MelodyBox, a note sequencer / player (SFX 519).

  python -B src/melodybox/build.py --out P    # P = scratchpad until the first release
  python -B src/melodybox/build.py --selftest decode|pack --out P   # self-test (see below)

Knob 1 = mode: MENU (0), TRK1 (1, default) or TRK2 (2). A track: knobs
1-3 show items off .. off + 2 of "" (empty), steps 1..n, "END" (step name =
2 digits + value: "01___" rest, "01Hld" hold, "01_C2", "01C#2"); knob 2 =
the value of its step (0 rest, 1 hold, 2..38 = C2..C5), knob 3 = scroll
offset. MENU: knob 2 = option TEMPO / LENGTH / GRID / SOUND1 / SOUND2, knob
3 = its value (SOUND1 / 2: sound * 20 + level, "EP:70"), the names spell
the question. Knob maxes change at run time. Knobs
4-9 = locked storage (the patch field widths 8, 8, 8, 8, 27, 8; names
Data1..Data6, texts DON'T / TURN / THESE on each page). See melodybox.c.
The screen image is screen.png (128x64; pixels darker than mid-grey are
dark).

--selftest decode|pack: writes melodybox_selftest.h (generated, from
packer/melopack_vectors.txt) and builds with MB_SELFTEST (1 decode, 2 pack;
both vector sets and the packer together don't fit 8 flash blocks): after a
fresh state the audio call decodes one vector per call with
packer/melopack.h (decode: the P lines' words and the U lines; pack: the P
lines' words) and compares n, the step flag and an FNV-1a hash of both
track grids + the tempo code; the pack build then packs each P line's grids
and tempo (mp_start + mp_run, a few units per call) and compares the key,
bits, parts and words.
The result goes into MeloBox's own knob 1 record column (read with
`g1on_sysex.py patch`): 0 running, 1 all passed, 2000 + i decode vector i
(0-based, file order) failed first, 3000 + i pack vector i (0-based, P
lines only) failed, 4000 + i its emitted bits differ from the DP's. Not
for a release; knob 1 is the MENU / TRACK knob now, so the result moves
the editor's mode (use the self-test build only for the packer test).
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

FXID = 519
MAX_K3 = 211                                            # mirrors MB_MAXK3 in melodybox.c
MAX_VAL = 38                                            # mirrors MB_MAXVAL in melodybox.c
ZMENU = PROJECT / "src" / "common" / "zmenu"
PACKER = HERE / "packer"
NOTEBUS = PROJECT / "src" / "common" / "notebus"
MIDIBUS = PROJECT / "src" / "common" / "midibus"
SPACE = 3                                               # --opt_for_space, as SongBox / SndBank (the packer: size)
PACK_KEEP_PLAIN = 4     # pack self-test: P lines with 64 steps, one track, 2 parts kept (fit 8 flash blocks)
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


def fnv(data: bytes) -> int:
    """FNV-1a 32 (mirrors mb_fnv in melodybox.c)."""
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def write_selftest(path: Path, pack: bool) -> int:
    """melodybox_selftest.h from packer/melopack_vectors.txt (pack: the P
    lines only, with their pack vectors, but only the first PACK_KEEP_PLAIN
    of the alike 64-step, one-track, 2-part lines; else all lines, no pack
    vectors); returns the number of decode vectors. Each: words (4 per
    part) in one pool; vec = word offset | parts << 16 | step flag << 20 |
    log2(n / 16) << 24 | sb << 28 (part 2 is SndBank's), then the FNV-1a hash
    of grid 1 + grid 2 + the tempo code byte. Pack vectors (P lines) also:
    pvec = grid offset in the byte pool (grid 1, then grid 2 if not empty;
    < 4096) | tempo code << 12 | log2(n / 16) << 20 | step flag << 22 |
    track 2 not empty << 23 | parts << 24 | key << 28, then token bits | sb
    << 15 | word offset << 16 (their words: the same pool)."""
    pool: list = []
    vecs: list = []
    pvecs: list = []
    grids = bytearray()
    plain = 0
    for line in (PACKER / "melopack_vectors.txt").read_text().splitlines():
        f = line.split()
        if not f or f[0] not in ("P", "U"):
            continue
        grid, grid2 = bytes.fromhex(f[-2]), bytes.fromhex(f[-1])
        code = {16: 0, 32: 1, 64: 2, 128: 3}[len(grid)]
        if f[0] == "P":                     # P bits key tempo step sb parts words.. grid grid2
            bits, key, tempo, step, sb, parts = (int(x) for x in f[1:7])
            words = f[7:7 + 4 * parts]
            has2 = int(any(grid2))
            if pack and len(grid) == 64 and not has2 and parts == 2:
                plain += 1
                if plain > PACK_KEEP_PLAIN:
                    continue
            if pack:
                assert len(grids) < 4096
                pvecs.append((len(grids) | tempo << 12 | code << 20 | step << 22 | has2 << 23
                              | parts << 24 | key << 28, bits | sb << 15 | len(pool) << 16))
                grids += grid + (grid2 if has2 else b"")
        elif pack:
            continue
        else:                               # U sb parts step tempo words.. grid grid2
            sb, parts, step, tempo = int(f[1]), int(f[2]), int(f[3]), int(f[4])
            words = f[5:5 + 4 * parts]
        vecs.append((len(pool) | parts << 16 | step << 20 | code << 24 | sb << 28,
                     fnv(grid + grid2 + bytes([tempo]))))
        pool += [int(w, 16) for w in words]
    lines = ["/* Generated by build.py --selftest from packer/melopack_vectors.txt -- do not edit. */",
             f"#define MB_NTEST {len(vecs)}u",
             f"#define MB_NTWORDS {len(pool)}u",
             f"#define MB_NPTEST {len(pvecs)}u",
             f"#define MB_NGRID {len(grids)}u",
             "static const struct {",
             "    uint32_t vec[MB_NTEST][2];",
             "    uint32_t words[MB_NTWORDS];"]
    if pack:
        lines += ["    uint32_t pvec[MB_NPTEST][2];",
                  "    uint8_t grids[MB_NGRID];"]
    lines += ["} mb_test = {", "  {"]
    lines += [f"    {{ 0x{a:08X}u, 0x{b:08X}u }}," for a, b in vecs]
    lines += ["  }, {"]
    for i in range(0, len(pool), 6):
        lines.append("    " + " ".join(f"0x{w:08X}u," for w in pool[i:i + 6]))
    if pack:
        lines += ["  }, {"]
        lines += [f"    {{ 0x{a:08X}u, 0x{b:08X}u }}," for a, b in pvecs]
        lines += ["  }, {"]
        for i in range(0, len(grids), 16):
            lines.append("    " + " ".join(f"{b}," for b in grids[i:i + 16]))
    lines += ["  }", "};", ""]
    path.write_bytes("\r\n".join(lines).encode("ascii"))
    return len(vecs)


def build(out_zdl: Path, selftest: str = "", probe: bool = False) -> None:
    from linker import LinkerConfig, link, params_from_manifest  # noqa: E402

    def knob(name: str, mx: int, getstr: str, d: int = 0) -> dict:
        return {"name": name, "type": "knob", "max": mx, "default": d, "scale": "enum",
                "audio_min": 0.0, "audio_max": float(mx), "audio_default": float(d),
                "getstr": getstr}

    params = [
        knob("", 2, "GetString_Mb1", 1),               # mode: MENU (0) / TRK1 (1) / TRK2 (2)
        knob("01___", MAX_VAL, "GetString_Mb2"),       # step value / MENU option
        knob("02___", MAX_K3, "GetString_Mb3"),        # scroll / MENU answer
    ]
    # knobs 4-9: locked storage at the patch field widths (8, 8, 8, 8, 27, 8)
    # names Data1..Data6, texts "DON'T TURN THESE" on both pages (knobs 4-6, 7-9)
    params += [{"name": f"Data{k + 1}", "type": "knob", "max": (1 << w) - 1, "default": 0, "store": True,
                "scale": "enum", "audio_min": 0.0, "audio_max": 100.0, "audio_default": 0.0,
                "edit": "Mb_LockEdit",
                "getstr": f"GetString_MbP{k + 4}" if probe else f"GetString_MbData{k % 3 + 1}"}
               for k, w in enumerate(FLOPPY_MAXES[3:])]
    obj = HERE / "melodybox.obj"
    defines = []
    if selftest:
        pack = selftest == "pack"
        print(f"  self-test ({selftest}): {write_selftest(HERE / 'melodybox_selftest.h', pack)} decode vectors")
        defines = [f"MB_SELFTEST={2 if pack else 1}"]
    if probe:                                           # knobs 4-9 show the EDITED differences (G1on)
        defines.append("MB_DIRTY=1")
    compile_c(HERE / "melodybox.c", obj, include=[ZMENU, NOTEBUS, MIDIBUS, PACKER], defines=defines, space=SPACE)
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
        screen_image=make_screen(),
        # no firmware knobs on the picture: own blank 1x1 knob graphic (11 frames)
        knob_positions=[(i + 2, x, y) for i, (x, y) in enumerate(KNOB_SLOTS)],
        knob_bitmap=(1, 1, 11, bytes(11)),
        dsp_cost=3.0,                                  # estimate: two own voices (8 samples each, saw + low-pass); not measured
        **link_common(),
    ))
    obj.unlink(missing_ok=True)


def main() -> None:
    args = sys.argv[1:]
    selftest = ""
    probe = "--probe" in args
    if probe:
        args.remove("--probe")
    if "--selftest" in args:
        i = args.index("--selftest")
        selftest = args[i + 1] if i + 1 < len(args) else ""
        del args[i:i + 2]
    if len(args) == 2 and args[0] == "--out" and selftest in ("", "decode", "pack"):
        out = Path(args[1]).resolve()
    else:
        sys.exit("usage: python -B src/melodybox/build.py [--selftest decode|pack] [--probe] --out P")
    out.parent.mkdir(parents=True, exist_ok=True)
    build(out, selftest, probe)


if __name__ == "__main__":
    for stream_ in (sys.stdout, sys.stderr):
        stream_.reconfigure(encoding="utf-8", errors="replace")
    main()
