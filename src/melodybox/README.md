# MelodyBox — note sequencer / player (in development)

MelodyBox (`MeloBox`) plays two tracks of notes (for example a melody
and a bass line) through SndBank (`src/sndbank/`), the sound source, in the
same patch. Without SndBank, MelodyBox plays both tracks itself with a simple
built-in voice each (a soft sawtooth synth); of the SOUND settings it then
uses only the volume. The guitar signal passes through.

- **Knob 1** picks the mode: MENU (settings, the first value), TRK1 or
  TRK2 (edit that track's steps).
- **TRK1 / TRK2: up to 128 steps** per track, shown three at a time on knobs 1-3. A knob's
  name is its step number (two digits; after 99 it counts 00, 01, ...
  again) plus the step's value: `01___` rest, `01Hld` hold (the note
  before goes on), `01_C2` a note, `01C#2` a sharp note. Notes go from C2
  to C5. The list starts with an empty item and ends with `END`.
- **Play position:** while the footswitch is on, knob 3's value (SCRLL)
  blinks with where playback is: `St:01` (steps 1-4), `SCRLL` (5-16),
  `St:17` (17-20), `SCRLL`, `St:33`, ..., `S:113`. Turn knob 3 to that
  step to see it.
- **Knob 3 ("SCRLL")** moves the view: turned right, the knobs show steps
  01-02-03, then 02-03-04, and so on. Both tracks keep the same view, so
  switching tracks shows the same steps.
- **Knob 2** sets the value of the step it shows (rest, hold, C2 … C5).
  After a scroll it jumps to the new step's value. On steps 01, 05, 09, 13, ...
  its value text shows a small mark instead of the step number.
- **MENU:** knob 2 picks the setting, the knob names ask its question and
  knob 3 gives the answer:

  | Setting | Question | Values |
  |---|---|---|
  | TEMPO | Song tempo is: | Pedal (default: the pedal's tempo), 40 … 250 BPM (not used while SongBox leads) |
  | LENGTH | Steps in song: | 16 (default), 32, 64, 128 (both tracks) |
  | GRID | Step grid is: | 1/16 (default), 1/8 (both tracks) |
  | SOUND1 | Trk1 sound is: | SndBank's sound and its volume, shown as `EP:70`: sounds PB (default), Tr, OJ (jazz organ), OR (rock organ), EP, Mr (marimba), Kl (kalimba), Sy (synth), Pk (pluck), each at volume 00 (silent) … 95, 50 = normal (default) |
  | SOUND2 | Trk2 sound is: | the same |

- **Footswitch on plays** both tracks in a loop at the TEMPO setting; a rest
  is silent, holds make the note before longer. Footswitch off stops
  (while SongBox leads, it only mutes).
- **Drums:** a [DrumBox](../drumbox/README.md#synced-to-melodybox) in the
  same patch plays its own beats and fills along: at MelodyBox's TEMPO,
  starting at beat 1 with step 1 and stopping with it, kept in time at
  every quarter note. After a patch change MelodyBox waits until DrumBox's
  kit is ready, then both start. With a SongBox that has a song, SongBox
  drives DrumBox instead.
- **Follows [SongBox](../songbox/README.md):** with a SongBox in the same
  patch that has a song, SongBox is the clock: MelodyBox plays at the
  song's position (counted from its bar 1), tempo and swing, and starts
  and stops with it. The melody starts again at step 1 each time the song
  loops (a melody shorter than the song repeats inside it). Added while
  the song plays, MelodyBox comes in at once at the right place. With an
  empty song MelodyBox leads, as without SongBox.
- **Knobs 4-9 hold stored data** (the start of the saved melody) and
  are locked: a turn springs back. They are named Data1 … Data6 and show
  `DON'T` `TURN` `THESE` on each page.

Status: the step editor, both tracks, the settings, playback over SndBank
and the own voices, saving / loading with the patch, DrumBox following
and following SongBox work on the B1Xon 1.30 (G1on family). Untested on the MS pedals.

## Saving with the patch

The melody is saved after each change (about half a second after the last
turn, like SongBox); store the patch as usual to keep it.

- **Where:** MelodyBox's own knobs 4-9 first, then SndBank and Floppy
  effects placed after MelodyBox (up to the next effect that saves its own
  data in Floppies). TEMPO, LENGTH and GRID are saved with the melody (a
  TEMPO other than Pedal takes a little room). SndBank also keeps SOUND1 and
  SOUND2 (sound and volume); without SndBank they are not saved.
- **Too big:** an edit that makes the melody too big for that space is
  refused: the step (or TEMPO / LENGTH) goes back and knob 2's name shows `FULL`
  for a moment. Add a Floppy for more space.
- **Adding or removing** SndBank or a Floppy: the melody you have stays and
  is saved into the new set. If it doesn't fit any more, `FULL` shows and
  nothing is saved, but your edits are kept: make the melody smaller step
  by step until it fits, then it is saved again.
- **Not with SongBox in the same patch for now:** SongBox would take every
  Floppy as its own.
- LENGTH keeps only the steps up to it: steps after a shorter LENGTH are
  not saved.

- **Up to 128 steps.** Melody length 16, 32, 64 or 128 steps; step
  length 1/16 or 1/8 (8 bars of quarters and eighths fit in 64 steps of
  1/8).
- **Both tracks in one chain** of storage effects: track 2 follows track 1,
  so a short bass line takes only the space it needs; an empty track 2
  takes none.
- **Each step:** rest, hold (the note before goes on) or a note in a
  3-octave range. A pause before a note is just a rest step; a dotted note
  is a note with holds.
- **Saved in MelodyBox's own knobs 4-9, then SndBank and Floppy
  effects** placed after MelodyBox, packed: lengths and
  holds cost almost nothing, small steps between notes are cheap (both
  tracks share one key), and
  repeated rhythms or bars (also moved up or down the scale) are stored
  as short references. A typical melody fits MelodyBox + SndBank, some
  need one Floppy more; a melody with a bass line usually needs 1 Floppy
  (sometimes none, sometimes 2).

## Files

| File | Purpose |
|---|---|
| `melodybox.c` | Audio function `Fx_SFX_MeloBox`: step lists (2 tracks), knob names, step editing, MENU, playback |
| `build.py` | Runs `cl6x` and the linker → `MeloBox.ZDL` (SFX, gid 7, fxid 519) |
| `packer/melopack.py` | Model of the storage format (the spec): packer, unpacker, size table and tests |
| `packer/melopack.h` | The format in C (decoder + packer), checked against the model on the pedal (`build.py --selftest decode` / `pack`) |
| `packer/melopack_vectors.txt` | Test vectors written by `melopack.py`, for checking the C version |
