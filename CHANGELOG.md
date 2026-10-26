# Changelog

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
