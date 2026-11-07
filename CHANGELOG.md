# Changelog

## Unreleased

New effects (in `src/`, built separately from DrumBox):

- **SongBox 0.7 (beta):** chord song sequencer. Songs of up to 128 bars
  (chord + one of 3 rhythms each, 149 patterns: nearly one per DrumBox style
  beat plus variations and arpeggios) written on the pedal,
  played by electric piano, jazz or rock organ (bass + chords), with MENU
  settings (play once / loop, chord octave, chord every bar / half / beat).
  Drives DrumBox in the same patch. Works on the B1Xon 1.30; MS pedals
  untested. 8 flash blocks (32,635 B): all instruments run on one engine
  (an instrument is data), rarely-run code is called instead of inlined,
  rhythms are stored sparse, all tables sit in one struct, texts in one
  string pool.
- `build/linker.py`: new options `rela_const_base` (the `.const`
  relocations share one dynamic symbol, offsets as addends, as Zoom's own
  effects do) `drop_attributes` and `anon_edit_syms` (knob handler symbols without
  names); `compact_edit_stubs` now also takes a
  knob over 150 (the shared handler gets the 200-step law); calls from the
  audio function into `.text` are fixed.
- **Floppy:** silent storage effect that saves SongBox's song in the patch
  (106 bits per copy, up to 4 copies). 1 flash block (3,691 B).
- **zmenu** (`src/common/zmenu/`): C library for knob menus and data saved
  through Floppy, used by SongBox.
- `build/zdl_build.py`: shared build helpers for SongBox and Floppy.

DrumBox:

- Moved to `src/drumbox/`.
- Follow mode also knows the MS-50G 3.10 and MS-60B 2.10 (untested).
- Tempo now goes up to 250 BPM (was 240).
- Count-in is always 2 bars, and only when the song starts while the kit is
  still rendering. A Kit change no longer stops the song: the beat goes on
  and the new drums come in as they are ready.
- Beats and fills are kept in `src/drumbox/patterns/patterns.json` (data for a future
  editor) instead of Python code; the built effect is unchanged by this.
- Smaller knob setup code at load (-288 B), still 8 flash blocks.

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
