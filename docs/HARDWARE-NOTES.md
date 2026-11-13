# Hardware notes

Things learned while bringing DrumBox up on a **Zoom G1Xon (firmware 1.21)**.
DrumBox is built for the MS-70CDR (firmware 2.10) toolchain, and the same
ZDL runs on the G1Xon unchanged. Every finding here was tested on the G1Xon
unless it says otherwise. For the ZDL format, the `ctx` map and the general
DSP rules, see repeat98's `build/ABI.md` and `docs/SAFE-DSP-RULES.md`.

## Load limit: code + tables

The G1Xon freezes when the effect is selected if its allocated sections
are too big. What counts is the **sum** of `.text` + `.const` + `.fardata`
(the two `total` lines of the linker log). Neither the file size nor either
section on its own decides it.

| Allocated sum | Result |
|---|---|
| 30,104 B | loads (largest known to work, SongBox 1.0.0) |
| 30,096 B | loads |
| 29,968 B | loads |
| 30,136 B | freezes on select (a patch holding it hangs the boot logo; only All INITIALIZE helped) |
| 30,216 B | freezes on select |
| 30,360 B | freezes on select |

SongBox 1.0.1 allocates 29,852 B, DrumBox 1.1.2 28,740 B. Check this
number before every upload.
Things that saved space, in the linker (`build/linker.py`):

- Own knob handlers (`build/own_handlers.asm`): one shared knob body plus
  a 16-byte entry stub per knob, instead of copies of a stock handler
  (DrumBox −800 B of load against its LineSel-based 1.1.1).
- The 40-byte `Dll_` (Leemuzhko's `dll.asm`) instead of a 200-byte copy
  of a stock one (−160 B).
- No float-divide helper (the effects use reciprocals).

In the DSP code:

- `--opt_for_space=3 --disable_push_pop` (about 8 KB less than no `-ms`).
  Without `--disable_push_pop`, `-ms` calls `__c6xabi_push_rts` /
  `pop_rts`, which the pedal does not provide. `-ms2` and `-ms3` both load
  and play.
- Data in `static const` tables (next section) instead of immediate stores:
  a table word costs 1–4 B, an immediate store about 11 B of code.
- Palettes: a table of distinct words plus one index byte per entry.
- Formulas instead of long compare chains: a 14-way chain of macros cost
  96 B.

## `static const` tables need load-time relocations

Every stock ZDL loads `.const` addresses with MVKL/MVKH pairs that have
`.rela.dyn` `R_C6000_ABS_L16` / `ABS_H16` entries. The firmware places
`.const` at load time and patches those pairs. A linker that patches the
pairs only at link time leaves the address wrong on the pedal. This
repository's linker gives every `.obj` MVKL/MVKH of a `.const` address a
`.rela.dyn` entry, and the tables then work. The linker log must say
`Applied N .obj relocations (N absolute, each with a .rela.dyn entry)`, with
both numbers equal. Reading the tables is not slower than reading `ctx[3]`.

## Saved knob values: `_init` with the `ctx[34]` call

Without an `_init`, a patch's saved knob values do not reach `params[]`
until each knob is touched. themanro's materializing `_init` calls every
knob's edit handler once. On the MS-70CDR that is enough. On the G1Xon
it froze the pedal (9 calls) or left the page-1 knobs dead (1 call),
until the `_init` first made the call that every stock `_init` makes:

```
ctx[34](ctx[1], zeros, 4 * (5 + knobs))
```

(LineSel's passes 28 zero bytes.) With it, saved values load on add, on
patch load, on patch switch and at power-up. Source:
`build/init_param_setup.asm`, linker option `init_param_setup`.

## Knob values

- **Raw value = knob value / 100, whatever the knob's max.** Knob 6 of a
  max-6 knob arrives as 0.06. An enum knob with fewer than 100 steps
  therefore gets one exact step per entry. Read it as
  `int(raw * 100 + 0.5)`.
- The knob handler converts the value with the firmware's
  `state[21](top, 0, steps, 0, v)`. The law `(1.5f, 151)` (as stock
  LineSel) stops at 150; knob maxes up to 150 work with it, including
  101..150.
- **Max 200** works with the law `(2.0f, 201)` (as stock APanDLY; raw up
  to 2.0). The linker picks it for the shared knob body when a knob's max
  is 200 or 210 (the law doesn't clamp past 200: a max-210 Tempo knob
  reaches 250 BPM). Other maxes above 150 need their own entry
  (`_OWN_RANGE_LAW`).
- 9 parameters (3 pages of 3 knobs) work.
- A `getstr` callback (descriptor `+0x24`) shows text for a knob value
  (names, BPM). Keep these callbacks free of loops, calls and division.

## Hiding the firmware's knobs

The firmware draws a knob graphic at each of the 3 knob slots in
`effectTypeImageInfo`, on top of the picture. The 24-byte knob info is
`{w, h, frames, 0, style, ptr}`:

- style 2, 3, 5, ... = a built-in graphic (ptr 0);
- style 255 or 12 = the effect's own bitmap at ptr: `frames x w x
  ceil(h/8)` uncompressed column bytes;
- frames = pointer positions of one knob (11 = 0 %..100 % in 10 % steps).

DrumBox uses its own 1x1 knob with 11 blank frames (linker `knob_bitmap`)
and puts the slots in a black corner of the picture. That hides the knobs
completely, so the picture can draw its own.

## Screen image

128x64, 1 bit per pixel, RLE-compressed (detail costs flash). The pedal's
pixels are about **1.33 times taller than wide**: draw circles 1.33x wider
than tall, and scale previews 3:4.

## Effect memory (`ctx[3]`) and patch switches

- Each effect slot gets a large per-instance arena at `ctx[3]`: **704 KB**
  on the G1Xon.
- **Don't trust its contents across patch switches.** The pedal lends the
  slot's memory to the other patch's effects and gives it back with the
  state header (magic + version) intact but the rest overwritten. DrumBox
  checksums its kit cache in the background and renders it again on a
  mismatch.
- A patch switch can be detected as a gap between audio calls: calls come
  every ~33,000 TSCL cycles, and a gap of more than ~9,000,000 (~50 ms)
  means the slot ran something else.
- **The next effect gets your leftovers.** No code runs when an effect is
  replaced, and the next effect in the slot gets the arena as it is. A
  stock delay uses the whole arena as a float delay line: it writes from
  the end downwards and, for its first delay time, reads from the start,
  which it only overwrites after a full pass (~4 s). Data left at the
  start plays through the delay: int16 samples gave ~1 s of loud noise,
  scattered variables gave a click on every later delay change until a
  restart. DrumBox stores its cache so that any 32-bit word reads as a
  float below -150 dB, and keeps its variables at the arena's end.

## Firmware math functions

The firmware puts some of its own math routines in the `ctx` table the
audio function receives. They are ordinary C functions (TI EABI: float
arguments in A4 / B4, result in A4), called through the word in the slot:

```c
typedef float (*ZdlF1)(float);
float s = ((ZdlF1)(uintptr_t)ctx[49])(x);     /* sine, x in radians */
```

| Slot | Function |
|---|---|
| `ctx[39]` | divide(a, b) = a / b |
| `ctx[40]` | exp(x) |
| `ctx[45]` | log10(x) |
| `ctx[46]` | power(base, exponent) |
| `ctx[49]` | sine(x), x in radians |
| `ctx[51]` | int-to-float(i) |

Tested on the B1Xon (firmware 1.30) with a probe effect: every one gave
the right values, sine once per sample (8 calls per call of the effect)
without crackles. The slot list comes from
[Ziddle](https://berbasoft.com/ziddle/)'s firmware model, which also names
cosine (38), reciprocal (47), 1/sqrt (48) and sqrt (50); those are
untested. Each use is a function call, so the CPU cost per call is still
to be measured. Not checked on the MS pedals.

**Not used in the released effects:** the probe that worked was compiled
without `--opt_for_space`. With it (`-ms`, as our effects are built),
cl6x makes these calls through `__c6xabi_call_stub`, and three SongBox
builds made that way froze the pedal as soon as the effect was selected
(before its picture showed; once only an All INITIALIZE brought the pedal
back). The same builds without the firmware calls loaded. The cause is not
found yet.

## DSP load

- The C674x runs at about **180 MHz**, measured with `TSCL`, which is
  readable from effect code.
- Each effect declares a DSP cost (a float at the name entry's `+0x28`,
  linker `dsp_cost`). The firmware sums it over the patch and refuses
  chains above its budget: **210 on the G1on/G1Xon OS** (~228 on the
  MS-70CDR). repeat98's placeholder of 20 lets the pedal build chains it
  can't run, which then crackle. Declare an honest cost.
- The 210 is a float constant in the firmware's chain check: the
  [Ziddle](https://berbasoft.com/ziddle/) emulator's firmware reader finds
  210.0 in G1on-family 1.21 (and 225.0 in MS-60B 2.10). Its validator also
  rejects a cost of 0: no stock effect declares one.
- DrumBox's first engine synthesized every hit live and used up to ~85 %
  of a call's time. Rendering each kit once into a cache in `ctx[3]` and
  playing the cache brought playback to ~15 % (~35 % while a kit renders).
  DrumBox declares 60, SongBox 52, Floppy 1.
- **Stock effects' declared costs don't match their real load.** A test
  effect that busy-waits a set share of each call, in the slot of SongBox,
  found how much time five heavy patches really left: 13 %, 20 %, 18 %,
  43 % and 52 % of a call, while the other effects' declared costs left
  49.6, 52.2, 53.9, 57.2 and 59.5 of the 210. A compressor declared at
  ~31 took far more, a reverb at ~27 far less. So a cost for a new effect
  has to be tried in heavy patches on the pedal. SongBox declares 52: that
  keeps out a patch where it crackled (room 49.6) and lets in one where it
  played clean (room 52.2).
- Loops that store into state can be unrolled by `cl6x` into long DINT/RINT
  (uninterruptible) blocks. Check that `dis6x` output has no `DINT` / `RINT`.

## Flash

- The G1Xon file system holds files in 4,090-byte blocks (4,096-byte
  flash blocks with a 6-byte header: previous block, next block, size).
- **At most 200 files.** Uploading a 201st file over USB is refused when
  the pedal creates it, even with free blocks left: the upload fails before
  any data is sent, and nothing is written.
  Every file counts, not just effects: `FLST_SEQ.ZDT` and sample files
  too. With 200 effects and no `FLST_SEQ.ZDT`, the pedal starts and shows
  all 200. A full pedal needs a file deleted before another can be added.
  (The file table itself has room for 255 entries, per Ziddle's reader;
  whether the pedal would show more than 200 files written some other way
  is untested.)
- Bytes +6..7 of the file system header read `C8 00` = 200, in the G1Xon
  1.21 and MS-70CDR 2.10 updater images and in this pedal's flash (the
  next two bytes are the total block count). It is likely the stored file
  limit; not tested by changing it.
- Stripping debug sections gains nothing for this linker's output (it
  writes none), and the stock effects on the G1Xon are already stripped.

## Effect list (`FLST_SEQ.ZDT`)

- The effect menu comes from `FLST_SEQ.ZDT` when that file exists. A newly
  uploaded effect that isn't listed in it does not show up. Deleting the
  file and restarting the pedal makes it scan the `.ZDL` files and list the
  new effect. Back the file up first.
- The pedal writes a new `FLST_SEQ.ZDT` by itself, at a restart or with
  the next upload. The list written during an upload holds that effect
  but not later ones. So upload all new effects first, delete the list
  last, then restart. The new list is a file too, and counts toward the
  200.
- Format (from Ziddle's reader): exactly 316 records of 13 bytes (a name of
  up to 12 characters plus a NUL), 4,108 bytes. Each category is a group:
  `>>>` + NUL + group id, the effect file names, all-zero blank records,
  then `<<<` + NUL + group id.

## USB file transfer (G1Xon)

`tools/g1on_sysex.py` (experimental, development use; see
`tools/README.md`) lists, deletes and
uploads single effects over USB MIDI SysEx, without a firmware update and
without Zoom Effect Manager 2. File names on the pedal are upper case. Updating an effect means delete + upload.
It also reads the current patch (`patch`: editor mode `50`, then request
`29`) and sets one parameter live (`knob`: `31` slot, param, value low and
high 7 bits; param 0 on/off, 2..10 knobs 1..9). Both keep unsaved edits;
`51` (editor mode off) and `53` (PC mode off) reload the patch and drop them.
`program N` switches to patch N (0..99) with a plain MIDI Program Change
(any channel; no editor mode needed, nothing written to flash, unsaved
edits dropped). Switch to a patch without custom effects before a replug,
so the pedal can't boot into an effect that fails to load.
`tools/g1on_keepalive.py` pings the pedal in the background.

- After a few minutes idle, or after a 4 KB SysEx reply, the pedal can stop
  answering SysEx. Only a **physical replug** wakes it; a Windows device
  restart does not.
- Send the identity request once. Further sends to a deaf pedal jam the
  Windows MIDI driver until a replug.
- Raw flash reads in 768-byte chunks work. Larger replies get dropped.
