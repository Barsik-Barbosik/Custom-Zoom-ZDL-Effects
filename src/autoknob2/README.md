# AutoKnob 2 — two LFOs that move knobs of other effects

<img src="../../docs/images/autoknob2.png" width="538" height="358" alt="AutoKnob 2 screen image">

AutoKnob 2 makes no sound. It sits in the same patch as other effects and
moves up to two of their knobs with two LFOs, for stock effects too: a
filter sweep on an auto-wah plus a tremolo on a level knob, or two knobs of
one effect moving against each other.

## Knobs

The knobs are a small menu, one page per LFO:

| Page | Knob 1 / 4 | Knob 2 / 5 | Knob 3 / 6 |
|---|---|---|---|
| 1 | LFO1: picks the row | first value of the row | second value of the row |
| 2 | LFO2: picks the row | first value of the row | second value of the row |
| 3 | Data1 (knob 7) | Data2 (knob 8) | Data3 (knob 9) |

Turning knob 1 (or 4) steps through four rows; knobs 2-3 (or 5-6) then show
and set that row's two values:

| Row | Value | Values | What it does |
|---|---|---|---|
| DEST | Slot | Off, 1..6 | Destination: which effect (chain position); shows the effect's name. Off = this LFO does nothing |
| DEST | Knob | 1..9 | Which of its knobs (up to the effect's last one); shows the knob's name |
| WAVE | Shape | Sine, Tri, Saw, Ramp, Sqr, S&H | LFO shape |
| WAVE | Rate | 0..100 | LFO speed, 0.05 to 10 Hz |
| RANGE | Low | 0..100 | Lowest value of the sweep, in % of the knob's range |
| RANGE | High | 0..100 | Highest value of the sweep, in % of the knob's range (below Low: the sweep runs upside down) |
| OTHER | Phase | 0..345 | Start offset in degrees, 15° steps |
| OTHER | Show | Off, On | On: the target's knob page follows the LFO |

The shapes (one cycle at the Rate's speed):

| Shape | How the knob moves |
|---|---|
| Sine | Smooth up and down between Low and High, slowing at both ends |
| Tri | Up and down at a steady speed, turning sharply at Low and High |
| Saw | From High down to Low at a steady speed, then a jump back to High |
| Ramp | From Low up to High at a steady speed, then a jump back to Low |
| Sqr | Half a cycle at High, half at Low |
| S&H | A random value between Low and High, held for one cycle, then a new one |

- **Page 3 (Data1..Data3) holds the menu's saved values.** The screen says
  "DON'T TURN THESE"; a turn is undone.
- **Both LFOs start Off** (Slot Off) when AutoKnob 2 is added to a patch.
- **The sweep runs between Low and High,** whatever the target knob is
  set to.
- **Same Rate on both LFOs: they stay locked together,** and Phase sets
  the offset between them (LFO2 Phase 180 = mirror image, 90 = a quarter
  behind). With different Rates each LFO runs on its own.
- **Both LFOs on the same knob:** LFO1 wins; LFO2 waits (its Knob shows
  "---").
- **Show Off (default): saving is safe.** The patch keeps the target
  knob's own setting, never an LFO value; the target's knob page shows
  that setting, not the moving value.
- **Show On:** the target's knob page follows the LFO. Don't save the
  patch while it runs: the save keeps the LFO value of that moment instead
  of your setting. Turning Show off, the footswitch off or another target
  puts your setting back. Show is untested on the MS pedals.
- **Footswitch off or another Slot / Knob:** the knob goes back to its own
  setting.
- **Never moved: Floppy, SndBank, AutoKnob and another AutoKnob 2** (Slot
  shows "---"). Floppy's and SndBank's knobs hold stored data, such as
  SongBox's song.
- **Removing AutoKnob 2 from the patch** leaves the knobs at their last LFO
  values until the patch is loaded again.

## Status

Version 1.0.0.

- **Works on the B1Xon 1.30,** tested with stock delay, reverb, chorus and
  compressor knobs.
- **G1on, G1Xon 1.21 and B1on 1.30** run the same firmware and should work
  too (untested).
- **MS-70CDR 2.10, MS-50G 3.10 and MS-60B 2.10** should work too
  (addresses from their firmware, untested): please report how it goes
  there.
- **Any other firmware:** AutoKnob 2 does nothing.
- Some stock effects may react badly to their knob handler being called
  this way: untested beyond those above.
- Slot counts chain positions: moving effects changes which effect an LFO
  points at.

## Install

`AutoKnb2.ZDL` (15,726 B, from the [Releases](../../../../releases) page or
`../../dist/`) needs 4 free flash blocks. It is in the **SFX**
category (gid 7, fxid 524) and declares a DSP cost of 10 (measured: about
2.5 to 4.5 % of the DSP on average). It is a separate effect from AutoKnob
(SFX 523); both can be installed at once. Install it as described in the
[main README](../../README.md#install) and add it to the patch next to the
effects whose knobs it should move.

## Files

| File | Purpose |
|---|---|
| `autoknob.c` | Audio function, the two LFOs, the knob menu and the knob texts |
| `build.py` | Writes `autoknob_rates.h`, runs `cl6x` and the linker → `../../dist/AutoKnb2.ZDL` |
| `autoknob_rates.h` | Generated: the Rate knob's phase steps |
| `screen.png` | The 128x64 screen image |
