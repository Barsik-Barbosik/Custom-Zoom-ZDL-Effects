# Melody Box — note sequencer / player (in development)

Melody Box (`MeloBox`) plays a sequence of notes through SndBank
(`src/sndbank/`), the sound source, which must be in the same patch.
Melody Box itself makes no sound and passes the audio through.

- **Knob 1** picks the mode: NOTE (edit the steps) or MENU (settings).
- **NOTE: up to 128 steps**, shown three at a time on knobs 1-3. A knob's
  name is its step number (two digits; after 99 it counts 00, 01, ...
  again) plus the step's value: `01___` rest, `01~~~` hold (the note
  before goes on), `01_C2` a note, `01C#2` a sharp note. Notes go from C2
  to C5. The list starts with an empty item and ends with `END`.
- **Knob 3 ("SCROLL")** moves the view: turned right, the knobs show steps
  01-02-03, then 02-03-04, and so on.
- **Knob 2** sets the value of the step it shows (rest, hold, C2 … C5).
  After a scroll it jumps to the new step's value.
- **MENU:** knob 2 picks the setting, the knob names ask its question and
  knob 3 gives the answer:

  | Setting | Question | Values |
  |---|---|---|
  | LENGTH | Number of steps: | 16 (default), 32, 64, 128 |
  | STEP | Each step is: | 1/16 (default), 1/8 |
  | SOUND | Play with sound: | SndBank's sounds PB (default), O1, O2, Sy, Tr, Or1, Or2, EP, Mrm, Klm, Syn, Plk |

- **Footswitch on plays** the steps in a loop at the pedal tempo; a rest
  is silent, holds make the note before longer. Footswitch off stops.

Status: the step editor and playback work on the B1Xon 1.30 (G1on
family). Untested on the MS pedals. The notes are not saved with the
patch yet: switching patches clears them (settings too).

## Planned: saving with the patch

The storage format is designed and tested in a Python model; the effect
does not use it yet.

- **Up to 128 steps.** Melody length 16, 32, 64 or 128 steps; step
  length 1/16 or 1/8 (8 bars of quarters and eighths fit in 64 steps of
  1/8).
- **Each step:** rest, hold (the note before goes on) or a note in a
  3-octave range. A pause before a note is just a rest step; a dotted note
  is a note with holds.
- **Saved in Floppy effects** placed after Melody Box, packed: lengths and
  holds cost almost nothing, small steps between notes are cheap, and
  repeated rhythms or bars (also moved up or down the scale) are stored
  as short references. Typical melodies need 1-2 Floppies.

## Files

| File | Purpose |
|---|---|
| `melodybox.c` | Audio function `Fx_SFX_MeloBox`: step list, knob names, step editing, MENU, playback |
| `build.py` | Runs `cl6x` and the linker → `MeloBox.ZDL` (SFX, gid 7, fxid 525) |
| `packer/melopack.py` | Model of the planned storage format: packer, unpacker, size table and tests |
| `packer/melopack_vectors.txt` | Test vectors written by `melopack.py`, for checking the C version |
