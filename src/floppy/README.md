# Floppy — storage effect for zmenu effects

<img src="../../docs/images/floppy.png" width="538" height="358" alt="Floppy screen image">

Floppy makes no sound. It sits in another slot of the same patch and holds
data for an effect that uses [zmenu](../common/zmenu/README.md) (e.g.
[SongBox](../songbox/README.md)): that effect writes its values into
Floppy's knobs, and the pedal saves them with the patch like any other
knob values.

- **9 knobs** ("Data1" to "Data9") at the largest values a patch field
  holds: 106 bits (13, 13, 13, 8, 8, 8, 8, 27, 8 bits per knob), measured
  on the G1on family and assumed the same on the MS pedals.
- **An empty Floppy reads as the defaults:** values are stored as the
  difference from their default.
- **More copies, more bits:** in zmenu stream mode, up to 4 Floppies in one
  patch (102 bits more per copy). The effect marks the extra copies itself;
  their order in the chain doesn't matter.
- **Don't turn its knobs:** that changes the stored data. Copying or saving
  the patch is safe.

Version 1.0.0. Status: works on the B1Xon 1.30 (G1on family). Untested on the MS pedals
(MS-70CDR 2.10, MS-50G 3.10, MS-60B 2.10): please report if saved data
comes back wrong there.

## Install

`Floppy.ZDL` (3,691 B, from the [Releases](../../../../releases) page or
`../../dist/`) needs 1 free flash block. It is in the **SFX**
category (gid 7, fxid 522). Install it as described in the
[main README](../../README.md#install) and add it to the patch next to the
effect that uses it.

## Files

| File | Purpose |
|---|---|
| `floppy.c` | Audio function `Fx_SFX_Floppy`: only the magic shuttle; optional knob texts (`FL_KNOB_TEXT`) |
| `build.py` | Runs `cl6x` and the linker → `../../dist/Floppy.ZDL` |
| `screen.png` | The 128x64 screen image |
| `screen.py` | Loads and checks `screen.png` (knob slots, RLE size); `--gen` redraws it |

## Build

Needs the TI C6000 Code Generation Tools (see the
[DrumBox README](../drumbox/README.md#build)) and `../common/zmenu/zmenu_gen.py`
(field widths, fxid).

```
python -B build.py                         # release build -> ../../dist/Floppy.ZDL (1 flash block)
python -B build.py --out PATH              # Floppy.ZDL to PATH
python -B build.py --knob-text --out PATH  # knob pages show the stored numbers (2 blocks)
```

The linker log should show 1 flash block (3,691 B) for the normal build.
