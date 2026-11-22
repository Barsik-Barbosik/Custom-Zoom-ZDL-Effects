# Melody Box — note sequencer / player (in development)

Melody Box (`MeloBox`) will play a sequence of notes. For now it is the
step editor only: it makes no sound and passes the audio through.

- **64 steps**, shown three at a time on knobs 1-3. A knob's name is its
  step number plus the step's note: `01___` (no note), `01__C`, `01_A#`.
  The list starts with an empty item and ends with `END`.
- **Knob 3 ("SCROLL")** moves the view: turned right, the knobs show steps
  01-02-03, then 02-03-04, and so on.
- **Knob 2** sets the note of the step it shows (none, C, C# … B). After
  a scroll it jumps to the new step's note.
- **Knob 1 ("NOTE")** does nothing yet.

Status: the step editor works on the B1Xon 1.30 (G1on family), tested
as a probe with the same code. Untested on the MS
pedals. The notes are not saved with the patch yet: switching patches
clears them.

## Files

| File | Purpose |
|---|---|
| `melodybox.c` | Audio function `Fx_SFX_MeloBox`: step list, knob names, note editing |
| `build.py` | Runs `cl6x` and the linker → `MeloBox.ZDL` (SFX, gid 7, fxid 525) |
