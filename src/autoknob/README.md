# AutoKnob — moves a knob of another effect

<img src="../../docs/images/autoknob.png" width="538" height="358" alt="AutoKnob screen image">

AutoKnob makes no sound. It sits in the same patch as another effect and
moves one of that effect's knobs up and down with an LFO, for stock effects
too: a filter sweep on an auto-wah, a tremolo from any level knob, a
wobbling drive.

| Knob | Values | What it does |
|---|---|---|
| Slot | 0..5 | Which effect (chain position); shows the effect's name, "--" for none |
| Knob | 0..8 | Which of its knobs; shows the knob's name |
| Wave | Sine, Tri, Saw, Ramp, Sqr, S&H | LFO shape |
| Rate | 0..100 | LFO speed, 0.05 to 10 Hz |
| Low | 0..100 | Lowest value of the sweep, in % of the knob's range |
| High | 0..100 | Highest value of the sweep, in % of the knob's range (below Low: the sweep runs upside down) |
| Show | Off, On | On: the target's knob page follows the LFO |

- **The sweep runs between Low and High,** whatever the target knob is
  set to.
- **Show Off (default): saving is safe.** The patch keeps the target knob's
  own setting, never an LFO value; the target's knob page shows that
  setting, not the moving value.
- **Show On:** the target's knob page follows the LFO. Don't save the patch
  while it runs: the save keeps the LFO value of that moment instead of
  your setting. Turning Show off, the footswitch off or another target puts
  your setting back. Show is untested on the MS pedals.
- **Footswitch off or another Slot / Knob:** the knob goes back to its own
  setting.
- **Never moved: Floppy and another AutoKnob** (Slot shows "--"). Floppy's
  knobs hold its stored data, such as SongBox's song.
- **Removing AutoKnob from the patch** leaves the knob at its last LFO value
  until the patch is loaded again.

Version 1.0.0. Status: works on the B1Xon 1.30 with a stock auto-wah and a
stock bass amp; the G1on, G1Xon 1.21 and B1on 1.30 run the same firmware
and should work too (untested). The MS-70CDR 2.10, MS-50G 3.10 and MS-60B
2.10 should work too (addresses from their firmware, untested): please
report how it goes there. On any other firmware AutoKnob does nothing. Some stock
effects may react badly to their knob handler being called this way:
untested beyond those two. Slot counts chain positions: moving effects
changes which effect it points at.

## Install

`AutoKnob.ZDL` (11,930 B, from the [Releases](../../../../releases) page or
`../../dist/`) needs 3 free flash blocks. It is in the **SFX** category
(gid 7, fxid 523) and declares a DSP cost of 6 (measured: about 2.5 % of
the DSP on average). Install it as described in
the [main README](../../README.md#install) and add it to the patch next to
the effect whose knob it should move.

## Files

| File | Purpose |
|---|---|
| `autoknob.c` | Audio function `Fx_SFX_AutoKnob`, the knob poke (`ak_poke`, `ak_store`) and the knob texts |
| `build.py` | Writes `autoknob_rates.h`, runs `cl6x` and the linker → `../../dist/AutoKnob.ZDL` |
| `autoknob_rates.h` | Generated: the Rate knob's phase steps |
| `screen.png` | The 128x64 screen image |
