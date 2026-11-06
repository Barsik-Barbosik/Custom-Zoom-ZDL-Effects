# zmenu — knob menus saved in the patch

zmenu is a C library for ZDL effects that need more values than their 9
knobs. It is not an effect itself: an effect includes `zmenu.h`, and the
values are saved in the patch through the [Floppy](../../floppy/README.md)
storage effect in another slot.

- **Menus:** a selector knob picks a record (e.g. a bar) or a parameter;
  the knobs after it edit that record's values. When the selector moves,
  the field knobs jump to the stored values.
- **Parameter menus:** one "Value" knob whose name changes to the
  selected parameter.
- **Saved in the patch:** all values are packed into Floppy's 9 knob fields
  as one bit stream; without Floppy they live in memory only and the
  selector shows "--".
- **Stream mode** (`ZM_STREAM 1`): no menus; the effect packs its own data
  and zmenu moves the bits in and out of up to 4 Floppies. Used by
  [SongBox](../../songbox/README.md).

Status: works on the B1Xon 1.30 (G1on family, OS image shared with the
G1on / G1Xon 1.21 / B1on). The MS-70CDR 2.10, MS-50G 3.10 and MS-60B 2.10
entries are untested (addresses read from their firmware); their patch
fields are assumed to hold the same 106 bits.

## Menu mode

1. Describe the menus in the effect's `build.py` and generate the layout
   header and the knob definitions:

   ```python
   from zmenu_gen import Menu, Field, generate
   menus = [
       Menu("Bar", records=4, fields=[Field("Beat", 128), Field("Chord", names=CHORDS)]),
       Menu("Param", params=[Field("Level", 101, default=50), Field("Wave", names=["Tri", "Sqr"])]),
   ]
   knob_params = generate(menus, HERE / "fx_zmenu.h", desc="_Fx_SFX_X_Desc")
   ```

2. In the effect: define `ZM_DESC` (its descriptor symbol), include
   `fx_zmenu.h`, keep a `ZmState` in `ctx[3]`, call `zm_poll(&zm, ctx)` at
   the top of every audio call and read values with `zm_val(&zm, slot)`
   (`ZM_SLOT_*` macros from the generated header).

- Menus take knobs 1.. in order, selector first. Menu knobs go up to 150.
- Names are up to 5 characters (the G1on screen).
- `generate(..., widths="common")` uses one layout for G1on and MS (106
  bits while MS is assumed equal), `"g1on"` the G1on widths.
- `packing="radix"` stores all values as one mixed-radix number, so a value
  costs log2(steps) bits instead of a whole number of bits (steps <= 256).

## Stream mode

Define `ZM_STREAM 1`, `ZM_DESC`, `ZM_MAGIC` and `ZM_FLOPPY_FXID` before
including `zmenu.h`, keep a `ZmStream` in `ctx[3]`, then:

| Call | When | Does |
|---|---|---|
| `zm_s_poll(&z, ctx)` | top of every audio call | Finds the Floppies; returns 1 when their data changed (patch loaded, Floppy added or removed) |
| `zm_s_get(&z, w)` | after `zm_s_poll` returned 1 | Reads all parts' bits into `w[]` |
| `zm_s_put(&z, w)` | after an edit | Writes `w[]` to the Floppies |
| `zm_s_jump(&z, ctx, k, v)` | when a knob must show another value | Sets the effect's own knob `k` to `v` |
| `zm_s_post(&z)` | end of the audio call | Posts the screen redraws |

- Part 1 holds 106 bits; each extra Floppy copy a 4-bit marker (12–14) and
  102 bits. The effect keeps its own first 4 bits below 12.

## Rules

- Every helper is force-inlined (no calls between code sections), no stack
  arrays, no integer divide.
- Pedal addresses are used only after the firmware fingerprint matched,
  checked every call (`ctx[3]` is lent to other patches' effects).
- The firmware table follows themanro's `zvis.h` (MS-70CDR), with added
  G1on, MS-50G and MS-60B entries (`zm_fws` in `zmenu.h`; also each OS's
  current patch number word).

## Files

| File | Purpose |
|---|---|
| `zmenu.h` | The library (menu mode and stream mode) |
| `zmenu_gen.py` | Menu / Field → `<effect>_zmenu.h` + knob params; Floppy field widths (`WIDTHS`) and `FLOPPY_FXID` |

The shared compile helpers (`find_ti_root`, `compile_c`, `encode_rle`,
`link_common`) are in `../../../build/zdl_build.py`.
