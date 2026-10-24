"""DrumBox screen image (128x64, 1 bpp).

The image is the PNG `screen.png` next to this file: pixels brighter than
mid-grey are white. build.py calls draw(), which loads it. Edit the PNG to
change the screen.

    python -B screen.py            # check screen.png (knob slots, RLE size)
    python -B screen.py --readme   # write ../docs/images/screen.png (README picture)

The pedal's pixels are about 1.33x taller than wide, so round things are
drawn 1.33x wider than tall, and the README picture scales each pixel 6
wide by 8 tall. The README picture comes from `screen_inverted.png`
(screen.png in black on white).

KNOB_SLOTS must stay black: build.py puts DrumBox's invisible 1x1 knobs
there (the firmware draws the knobs on top of the picture).
"""
from __future__ import annotations
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.append(str(HERE.parent / "build"))

from screen_image import Canvas  # noqa: E402

W, H = 128, 64
KNOB_SLOTS = [(1, 0), (126, 0), (127, 0)]
SOURCE = HERE / "screen.png"
README_SOURCE = HERE / "screen_inverted.png"
README_IMAGE = HERE.parent / "docs" / "images" / "screen.png"


def check_slots(c: Canvas) -> None:
    for x, y in KNOB_SLOTS:
        assert not any(c.pixels[y + z][x] for z in range(8)), f"knob slot {x},{y} not black"


def draw() -> Canvas:
    """The screen image, loaded from SOURCE."""
    from PIL import Image
    img = Image.open(SOURCE).convert("L")
    assert img.size == (W, H), f"{SOURCE.name} is {img.size}, need {W}x{H}"
    c = Canvas()
    for y in range(H):
        for x in range(W):
            c.px(x, y, 1 if img.getpixel((x, y)) > 127 else 0)
    check_slots(c)
    return c


def readme_image(path: Path = README_IMAGE, sx: int = 6, sy: int = 8) -> None:
    """README_SOURCE upscaled: each pixel sx x sy, black and white."""
    from PIL import Image
    src = Image.open(README_SOURCE).convert("L")
    assert src.size == (W, H), f"{README_SOURCE.name} is {src.size}, need {W}x{H}"
    img = src.point(lambda v: 255 if v > 127 else 0).convert("1")
    img = img.resize((W * sx, H * sy), Image.NEAREST)
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    print("wrote", path)


if __name__ == "__main__":
    from screen_image import encode_zoom_rle
    if "--readme" in sys.argv:
        readme_image()
    else:
        print(f"{SOURCE.name}: knob slots black, RLE {len(encode_zoom_rle(draw()))} B")
