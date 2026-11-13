# Changelog

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
