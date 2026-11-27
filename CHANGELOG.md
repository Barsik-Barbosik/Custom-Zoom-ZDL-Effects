# Changelog

## Unreleased: DrumBox + MelodyBox

DrumBox and MelodyBox must be released together (an older DrumBox doesn't
sync to MelodyBox).

### DrumBox

- **Synced to MelodyBox:** with MelodyBox in the patch, DrumBox plays its own
  song at MelodyBox's tempo, starts and stops with it and stays in time
  (lined up at every quarter note). DrumBox alone and with SongBox play as
  before.

### MelodyBox

- **DrumBox follows:** same tempo, starts together at step 1 / beat 1 (after
  a patch change once DrumBox's kit is ready), stops together.

## SongBox 1.14.1 (unreleased)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `27bdfe1fcc277328854f040ae9ed46892accb869ee193f549d31a6757ed06cc0` (1.10.1, unchanged) |
| `SongBox.ZDL` | 32,230 B | 8 | 52 | `e409b834a6ac7a84cd6cdf7593e056d4705ef6652e1aa6576e29ec48f9305a67` |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |
| `AutoKnb1.ZDL` | 12,146 B | 3 | 6 | `b880b5e5beb07f203f94cdff2e1f5f4c9506da1196158052be57bf314620e463` (1.10.0, unchanged) |
| `AutoKnb2.ZDL` | 15,726 B | 4 | 10 | `e8ae0e2845f279d9c8cdda043bca310315a60a1817b70e616bf64f88d74adb2c` (1.0.0, unchanged) |

The ZDL header still shows `1.14`.

### SongBox

- **Fix: switching patches could erase a Floppy in the next patch.** For a
  moment after a patch change SongBox still runs while the pedal already
  holds the next patch's effects. If that patch had Floppies that belong to
  another effect, SongBox could take them for its own and overwrite them.
  SongBox now leaves every Floppy alone once its own place in the patch
  holds another effect.

## AutoKnob 1.10.0, AutoKnob 2 1.0.0 (2026-11-22)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `27bdfe1fcc277328854f040ae9ed46892accb869ee193f549d31a6757ed06cc0` (1.10.1, unchanged) |
| `SongBox.ZDL` | 32,166 B | 8 | 52 | `aa8ef53dd7b5fa664d018043ad4d42fe117b9706feb8e0796d5630f8aed8508c` (1.14.0, unchanged) |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |
| `AutoKnb1.ZDL` | 12,146 B | 3 | 6 | `b880b5e5beb07f203f94cdff2e1f5f4c9506da1196158052be57bf314620e463` |
| `AutoKnb2.ZDL` | 15,726 B | 4 | 10 | `e8ae0e2845f279d9c8cdda043bca310315a60a1817b70e616bf64f88d74adb2c` |

The ZDL headers show `1.10` (AutoKnob) and `1.00` (AutoKnob 2).

### AutoKnob 2 (new)

- **Two LFOs, each moving a knob of another effect** in the same patch,
  stock effects too. Per LFO: Slot and Knob (the target), Shape (Sine,
  Tri, Saw, Ramp, Sqr, S&H), Rate (0.05 to 10 Hz), Low and High (the sweep
  range), Phase (0 to 345 degrees) and Show.
- The knobs are a small menu: knob 1 / 4 picks a row (TARGET, WAVE, RANGE,
  OTHER) of LFO 1 / 2, the two knobs next to it set that row's values.
  Page 3 holds the menu's saved values and can't be turned.
- With the same Rate the two LFOs run locked together; Phase sets the
  offset between them.
- Both LFOs start Off (Slot Off), Show Off.
- A separate effect (SFX 524): it can sit next to AutoKnob in the same
  patch. Floppy, AutoKnob and another AutoKnob 2 are never a target.

### AutoKnob 1.10.0

- **Renamed to AutoKnb1** (file `AutoKnb1.ZDL`), next to AutoKnb2. Same
  effect id (SFX 523), so saved patches keep it: delete the old
  `AutoKnob.ZDL` from the pedal when you install the new file.
- Slot goes up to the pedal's last chain position and Knob up to the
  target's last knob; past the end Slot shows "---".
- New default: Slot 0 (the first effect). Show stays Off by default.
- AutoKnob 2 is never a target.
- Version jumps from 1.0.0 to 1.10.0: the ZDL header shows major.minor
  ("1.10"), as for DrumBox and SongBox.

Tested on the B1Xon 1.30; the other G1on family pedals and the MS pedals
are untested.

## AutoKnob 1.0.0 (2026-11-20)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `27bdfe1fcc277328854f040ae9ed46892accb869ee193f549d31a6757ed06cc0` (1.10.1, unchanged) |
| `SongBox.ZDL` | 32,166 B | 8 | 52 | `aa8ef53dd7b5fa664d018043ad4d42fe117b9706feb8e0796d5630f8aed8508c` (1.14.0, unchanged) |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |
| `AutoKnob.ZDL` | 11,930 B | 3 | 6 | `4b31f629d196317280794d1daddc49d6941b6596813e16c4b2dcd5d469ff25aa` |

The ZDL header shows `1.00`.

### AutoKnob (new)

- **An LFO for another effect's knob.** AutoKnob makes no sound: it moves
  one knob of another effect in the same patch, stock effects too (a
  filter sweep on an auto-wah, a tremolo from a level knob, a wobbling
  drive).
- Knobs: Slot and Knob pick the target (shown by the effect's and the
  knob's names), Wave (Sine, Tri, Saw, Ramp, Sqr, S&H), Rate (0.05 to
  10 Hz), Low and High (the sweep range; High below Low sweeps upside
  down), Show (the target's knob page follows the LFO).
- Footswitch off or another target: the knob goes back to its own
  setting. With Show off, saving the patch keeps the knob's own setting.
- Floppy and another AutoKnob are never a target (Slot shows "--"):
  Floppy's knobs hold its stored data, such as SongBox's song.

Tested on the B1Xon 1.30 (stock auto-wah, stock bass amp); the other G1on
family pedals and the MS pedals are untested.

## SongBox 1.14.0 (2026-11-18)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `27bdfe1fcc277328854f040ae9ed46892accb869ee193f549d31a6757ed06cc0` (1.10.1, unchanged) |
| `SongBox.ZDL` | 32,166 B | 8 | 52 | `aa8ef53dd7b5fa664d018043ad4d42fe117b9706feb8e0796d5630f8aed8508c` |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |

The ZDL header shows `1.14`.

### SongBox

- **Bass and chords on their own knobs.** Volume and Instr are replaced
  by **Bass** and **Chords**: each picks a sound and its level, shown as
  "PB:70" or "Or1:55". The level goes from 00 (off) to 95 in steps of 5,
  50 = unchanged, as the old Volume. Any chords sound goes with any bass
  sound. Defaults PB:70 and Or1:70, as loud as before.
- Bass names: PB (precision bass, was BG), O1, O2, Sy (the organs' and
  the synth's basses, were B1, B2, BS), Tr (triangle, was BT).
- The Instr kits are gone, with the chords-only kits, the bass-alone kits
  and Mute: set a level to 0 instead. Or1, Or2 and Syn with their own bass
  are Or1 + O1, Or2 + O2 and Syn + Sy.
- Saved patches: both knobs read the old Volume / Instr values as new
  ones, so set them again.

Tested on the B1Xon 1.30; the MS pedals are untested.

## SongBox 1.10.2 (2026-11-17)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `27bdfe1fcc277328854f040ae9ed46892accb869ee193f549d31a6757ed06cc0` (1.10.1, unchanged) |
| `SongBox.ZDL` | 32,294 B | 8 | 52 | `38d3eea55dc65683362f9096c114fe8e49c68a848f011a381045cb0419e50f24` |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |

The ZDL header still shows `1.10`. Saved patches stay as they are.

### SongBox

Fixes for switching between patches that hold SongBox and DrumBox. The
pedal loads the new patch's knob values a moment after the effects start
running, and SongBox read them too early:

- The drums no longer go out of sync after a patch change. DrumBox could
  still be counting in (at the Tempo saved on DrumBox) when SongBox's
  first bar started.
- The new patch's song plays from the start, instead of the previous
  patch's rhythm for a moment.
- Switching between song patches no longer asks to save a patch you didn't
  change.

Tested on the B1Xon 1.30; the MS pedals are untested.

## DrumBox 1.10.1, SongBox 1.10.1 (2026-11-17)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `27bdfe1fcc277328854f040ae9ed46892accb869ee193f549d31a6757ed06cc0` |
| `SongBox.ZDL` | 32,294 B | 8 | 52 | `3fc8da493b6de3e738bbf06184ce3b407c74cff5cffde22441007cff31253452` |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |

The ZDL header still shows `1.10`. Saved patches stay as they are.

### DrumBox and SongBox

- The Tempo knob is a plain knob again: no own tap button on the MS pedals.
  With the tempo-knob flag of 1.10.0, an MS-70CDR offered a delay-style
  tap mode on the effect, which worked but differed from the pedal's TEMPO
  screen (there you can tap the footswitch or the knob). Tap the tempo on
  the pedal's TEMPO screen; at Pedal (the default) the effect follows it.
  Tested on the B1Xon; the MS pedals should behave as described (untested
  with 1.10.1).

## DrumBox 1.10.0, SongBox 1.10.0 (2026-11-17)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,663 B | 8 | 60 | `922cc79c9b49591840f79b5aca928587f239cdfbdd0ed2756a68a84319a89e51` |
| `SongBox.ZDL` | 32,294 B | 8 | 52 | `016bea7d65e901b6b2bc2b97c5a2a830f076d5f4ab68c730cb2facbb76099701` |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` (1.0.1, unchanged) |

All three work on the B1Xon 1.30; the MS pedals are untested.

Version numbers jump to 1.10.0 so they match the ZDL header's version
field, which holds 4 characters: the header shows major.minor (`1.10`), and
bug-fix releases (1.10.1) keep it. Floppy keeps its own numbering.

### DrumBox

- **Pedal Tempo (the new default):** the first Tempo value, before 40 BPM,
  follows the pedal's tempo (G1on family: the BPM of the settings menu and
  the rhythm / looper; MS pedals: tap tempo / TEMPO; MIDISTOMP: the MIDI
  clock's tempo, in whole BPM). The BPM values moved up one knob step, so
  patches saved with an older DrumBox play 1 BPM slower (re-set Tempo once).
  The Tempo knob carries the tempo-knob flag of Zoom's own delays, which
  should let the MS pedals offer tap tempo for it (untested on MS pedals).
  Tested on the B1Xon.
- Updated logo.

### SongBox

- **Pedal Tempo (the new default):** as DrumBox's, the first Tempo value
  follows the pedal's tempo, and a DrumBox it drives follows it too. The BPM
  values moved up one knob step: patches saved with an older SongBox play
  1 BPM slower (re-set Tempo once). Use it with this DrumBox (older DrumBox
  versions play 1 BPM faster when driven). The Tempo knob carries the
  tempo-knob flag of Zoom's own delays, which should let the MS pedals offer
  tap tempo for it (untested on MS pedals). Tested on the B1Xon.

## DrumBox 1.1.2, SongBox 1.0.1, Floppy 1.0.1 (2026-11-13)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | DSP cost | sha256 |
|---|---|---|---|---|
| `DrumBox.ZDL` | 31,511 B | 8 | 60 | `92b254f092d6cb3df7422f46fddc64d877660d6deab7673af1e16932c8f82287` |
| `SongBox.ZDL` | 32,094 B | 8 | 52 | `8baf7f1653438f4a5040cc14ee9bd2400237553ed15bdb66e00ca90f7358b2e9` |
| `Floppy.ZDL` | 3,499 B | 1 | 1 | `83bf4f0bb2b06ce1e8a53f0475ee62a6e1b4a69c307979af52760a338ba8f66f` |

All three work on the B1Xon 1.30; the MS pedals are untested.

### All effects

- **No code from Zoom's effects in the ZDLs any more.** Own OnOff and knob
  handlers (`build/own_handlers.asm`, with TI's call stub) and Leemuzhko's
  40-byte `Dll_` replace the stock LineSel handlers and the NoiseGate
  `Dll_`. The stock blobs are gone from the repository. Checked on the
  B1Xon 1.30: load, knobs, footswitch, and knob values after save, patch
  switch and power cycle.

### DrumBox 1.1.2

- **Kit knob:** a kit renders only once the Kit knob has rested ~145 ms,
  so turning through kits costs no render time.
- A drum lane's first sample is started in its own call (no spike when a
  drum starts).
- The 9 knob handlers are 16-byte stubs into one shared body (as SongBox
  and Floppy): 31,511 B (was 32,666), alloc 28,740 (was 29,540), still 8
  flash blocks.

### SongBox 1.0.1

- **Less CPU:** about 16 % of the DSP time per call on average (1.0.0
  ~21 %), and lower peaks:
  - the next step's notes are worked out mid-step (look-ahead), so the
    call where they start does only a copy (note start ~10 % → ~1 %);
  - faded voices run on silently, so their tables and code stay in the
    cache (the worst call ~60 % → ~45 %);
  - levels and LFOs once per 8-sample block, and a note starts at the
    start of a call (at most 7 samples early);
  - wavetables of 512 instead of 2,048 points, built 4x faster after
    loading or a patch change (sounds the same);
  - song checksums only after a patch change, a lighter packer and edit
    path: fewer clicks while turning Chord.
- **Instr knob:** the kit changes once the knob has rested ~145 ms, so
  turning through the kits doesn't click.
- Same sound as 1.0.0 (A/B tested on the pedal with the electric piano).
- **Declared DSP cost 52** (1.0.0 declared 45, too low: a patch could be
  accepted and then crackle). Measured in heavy patches: SongBox with
  stock effects that leave it less room crackled sometimes, so the pedal
  now refuses those patches.
- Smaller (32,094 B, alloc 29,852 of the ~30,104 that load): the song
  packer's small helpers are called instead of inlined, and the DrumBox
  beat per rhythm is stored as a small code.
- All 149 rhythms are still there.
- Updating from 1.0.0: songs saved in Floppy load as before. A song that
  was only in memory (not saved in a Floppy) is lost.

### Floppy 1.0.1

- Declared DSP cost 1 (was 2): Floppy does no audio work.
- 3,499 B (was 3,691), still 1 flash block.
- Songs saved with Floppy 1.0.0 load.

### Tools

- `tools/g1on_sysex.py` (renamed from `zoom1_files.py`) can also read the
  current patch (`patch`), set a knob live (`knob`) and switch patches
  (`program`); `tools/g1on_keepalive.py` (renamed from
  `zoom1_keepalive.py`). New `tools/README.md`.
- `build/linker.py`: the own handlers and the 40-byte `Dll_` are the only
  path; the LineSel / divf / knob3 / attributes options are gone. It
  refuses an object that needs the float-divide, push / pop or call-stub
  helpers.
- Docs: firmware math functions and why they aren't used yet
  (`docs/HARDWARE-NOTES.md`), cache misses on rarely-run work and the
  look-ahead (`docs/ZDL-CPU-TIPS.md`), new size tips.

## SongBox 1.0.0, Floppy 1.0.0, DrumBox 1.1.1 (2026-11-09)

Files in `dist/` and on the Releases page:

| File | Size | Flash blocks | sha256 |
|---|---|---|---|
| `SongBox.ZDL` | 32,326 B | 8 | `ef9239112477ede388b3abe0ca178bcd15ef6b1280cf92f6616d544ac4296790` |
| `Floppy.ZDL` | 3,691 B | 1 | `e25b75a41ecf67b85a990426155fe0dfde051a686033aaede7a3c6307f971dc4` |
| `DrumBox.ZDL` | 32,666 B | 8 | `7789e38c411ea85f66d2a688a97d59a163ddba5135bee74166bf4c1324fa3726` |

All three work on the B1Xon 1.30; the MS pedals are untested.

### SongBox 1.0.0 (new)

Chord song sequencer. Songs of up to 128 bars (chord + one of 3 rhythms
each, 149 patterns: nearly one per DrumBox style beat plus variations and
arpeggios) written on the pedal, played by Organ1 (jazz), Organ2 (rock),
electric piano, marimba, kalimba, synth (saw) or pluck chords with a bass
guitar (precision bass), Organ1's, Organ2's, the synth's or a triangle
bass (Instr knob: 48 kits: every chords and bass pairing, each chords part
alone, each bass alone, Mute; names like "Or1+BG", "EPCh", "B2"), with
MENU settings (play once / loop, chord octave, chord every bar / half /
beat). Saved in the patch through Floppy. Drives DrumBox in the same patch.

Since the 0.7 beta in the repository:

- A song written without Floppy is kept when an empty Floppy is added to
  the same patch, and saved into it (Full if it doesn't fit).
- Knob names are rewritten only when their text changes.
- Defaults: rhythms Rock / Rk16b / Randm, Volume 70.
- New screen picture; Rock plays only chord tones.
- A SongBox being replaced by a Floppy in its own slot never reads that
  slot as a Floppy.
- Size: all instruments run on one engine (an instrument is data),
  rarely-run code is called instead of inlined, rhythms are stored sparse,
  all tables sit in one struct, texts in one string pool copied by one
  routine, bar lists copied by one routine. Alloc 30,104 B, the largest
  known to load on the G1on family.

### Floppy 1.0.0 (new)

Silent storage effect that saves SongBox's song in the patch (106 bits per
copy, up to 4 copies).

### DrumBox 1.1.1

1.1.0 was not released on its own; its changes are in 1.1.1.

- **Follow mode:** with SongBox in the same patch, DrumBox plays the song's
  beats, fills and tempo bar by bar and starts and stops with it. Knows the
  G1on family, MS-70CDR 2.10, MS-50G 3.10 and MS-60B 2.10 (the MS pedals
  untested). DrumBox's footswitch off mutes the drums but keeps the song's
  place.
- Tempo now goes up to 250 BPM (was 240).
- Count-in is always 2 bars, and only when the song starts while the kit is
  still rendering. A Kit change no longer stops the song: the beat goes on
  and the new drums come in as they are ready.
- Beats and fills are kept in `src/drumbox/patterns/patterns.json` (data
  for a future editor) instead of Python code.
- Smaller knob setup code at load (-288 B), still 8 flash blocks.
- Moved to `src/drumbox/`.

### Tools and libraries

- `build/linker.py`: new options `rela_const_base` (the `.const`
  relocations share one dynamic symbol, offsets as addends, as Zoom's own
  effects do), `drop_attributes` and `anon_edit_syms` (knob handler symbols
  without names); `compact_edit_stubs` now also takes a knob over 150 (the
  shared handler gets the 200-step law); calls from the audio function into
  `.text` are fixed.
- **zmenu** (`src/common/zmenu/`): C library for knob menus and data saved
  through Floppy, used by SongBox.
- `build/zdl_build.py`: shared build helpers for SongBox and Floppy.
- `python -B build.py` without `--out` now builds SongBox and Floppy into
  `dist/`, as DrumBox already did.

## 1.0.1 (2026-10-26)

- Fixed: replacing DrumBox with a delay played about 1 s of loud noise.
  The kit cache stays in the slot's memory, and the delay played it as
  audio. Samples are now stored in a form that reads as silence.
- Fixed: after DrumBox had been used, every later change from one delay to
  another clicked until the pedal was restarted. DrumBox's own variables
  now sit at the end of its memory, which a delay overwrites first.
- Same size as 1.0.0 (8 flash blocks).
- Documentation: the main README holds only what all effects share (with
  a small screen picture, a demo video link and a download link per
  effect); DrumBox's features, install size and controls moved to
  `drumbox/README.md`. Its Controls table lists each knob's values.
  `docs/HARDWARE-NOTES.md` explains how a following effect gets the
  slot's memory.
- Linker: an `.obj` may declare `extern unsigned int _<audio>_Info[]`,
  resolved to the effect's `effectTypeImageInfo` (as in themanro's
  linker). No change to DrumBox's build.

## 1.0.0 (2026-10-24)

First release (`dist/DrumBox.ZDL`, 32,714 B, sha256
`98968175517719d139289d69008fee0012bda59fc366b51d8b69e8ff2d3644db`).

- In the SFX category.
- 8 lanes, 150 beats and 150 fills (including Random), 22 kits,
  two-section song with Skip and middle fills, Tempo 40-240 BPM.
- Saved knob values load with patches (G1Xon).
- Kits rendered once into a cache: ~15 % DSP while playing; the cache is
  checked and re-rendered after patch switches.
- Count-in while a kit renders: the closed hi-hat renders first and counts
  quarter notes; the song starts at beat 1 on the bar after the kit is
  ready, after 1 count bar or an even number.
- No crash fill on the song's first downbeat after a kit change or patch
  switch.
