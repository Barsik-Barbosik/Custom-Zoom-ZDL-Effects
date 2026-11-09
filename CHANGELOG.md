# Changelog

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
