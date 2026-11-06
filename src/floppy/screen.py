"""Floppy screen image (128x64, 1 bpp): a 3.5-inch floppy disk.

The image is the PNG `screen.png` next to this file: it looks as on the pedal:
pixels darker than mid-grey are dark (set bits in the ZDL picture).
build.py (build_floppy) calls draw(), which loads it. Edit the PNG to
change the screen.

    python -B screen.py            # check screen.png (knob slots, RLE size)
    python -B screen.py --gen      # (re)draw screen.png from generate()

The pedal's pixels are about 1.33x taller than wide, so the square disk is
drawn 1.33x wider than tall.

KNOB_SLOTS must stay light: build_floppy puts invisible 1x1 knobs there
(the firmware draws the knobs on top of the picture), as DrumBox does.
"""
from __future__ import annotations
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.append(str(HERE.parents[1] / "build"))

from screen_image import Canvas  # noqa: E402

W, H = 128, 64
KNOB_SLOTS = [(1, 0), (126, 0), (127, 0)]
SOURCE = HERE / "screen.png"


def _box(c: Canvas, x0: int, y0: int, x1: int, y1: int, v: int) -> None:
    for y in range(y0, y1 + 1):
        c.hline(x0, x1, y, v)


def generate() -> Canvas:
    """Dark disk body (cut top-right corner), metal shutter with its slot,
    write-protect hole, white label with a bold FLOPPY title."""
    c = Canvas()
    x0, y0, x1, y1 = 24, 2, 103, 61          # 80 x 60 = square on the pedal
    _box(c, x0, y0, x1, y1, 1)
    for k in range(8):                       # cut corner, top right
        c.hline(x1 - 7 + k, x1, y0 + k, 0)
    for x, y in ((x0, y0), (x0, y1), (x1, y1)):   # rounded corners
        c.px(x, y, 0)
    # shutter: light plate from the top edge, the head slot dark inside it
    _box(c, 44, y0, 87, 23, 0)
    c.vline(44, y0, 23, 0)
    _box(c, 74, 6, 82, 19, 1)
    # write-protect hole, bottom left; index hole, bottom right
    _box(c, 28, 52, 32, 57, 0)
    _box(c, 96, 52, 99, 57, 0)
    # label: light, rounded, FLOPPY in bold 3x5 glyphs (2 x 2 per pixel)
    lx0, ly0, lx1, ly1 = 36, 29, 91, 58
    _box(c, lx0, ly0, lx1, ly1, 0)
    for x, y in ((lx0, ly0), (lx1, ly0), (lx0, ly1), (lx1, ly1)):
        c.px(x, y, 1)
    glyphs = dict(Canvas._FONT, Y=["101", "101", "010", "010", "010"])  # font's Y reads as T
    x = 43
    for ch in "FLOPPY":
        for ry, row in enumerate(glyphs[ch]):
            for rx, bit in enumerate(row):
                if bit == "1":
                    _box(c, x + rx * 2, 33 + ry * 2, x + rx * 2 + 1, 33 + ry * 2 + 1, 1)
        x += 7
    for y in (47, 51, 55):                   # writing lines
        c.hline(40, 87, y, 1)
    return c


def check_slots(c: Canvas) -> None:
    for x, y in KNOB_SLOTS:
        assert not any(c.pixels[y + z][x] for z in range(8)), f"knob slot {x},{y} not light"


def draw() -> Canvas:
    """The screen image, loaded from SOURCE."""
    from PIL import Image
    img = Image.open(SOURCE).convert("L")
    assert img.size == (W, H), f"{SOURCE.name} is {img.size}, need {W}x{H}"
    c = Canvas()
    for y in range(H):
        for x in range(W):
            c.px(x, y, 1 if img.getpixel((x, y)) <= 127 else 0)
    check_slots(c)
    return c


def save_png(c: Canvas, path: Path) -> None:
    from PIL import Image
    img = Image.new("L", (W, H))
    for y in range(H):
        for x in range(W):
            img.putpixel((x, y), 0 if c.pixels[y][x] else 255)
    img.save(path)


if __name__ == "__main__":
    from screen_image import encode_zoom_rle
    if "--gen" in sys.argv:
        save_png(generate(), SOURCE)
        print("wrote", SOURCE)
    print(f"{SOURCE.name}: RLE {len(encode_zoom_rle(draw()))} B")
