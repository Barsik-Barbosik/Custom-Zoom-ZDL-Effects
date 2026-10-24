# Changelog

## 1.0 (2026-10-24)

First release (`dist/DrumBox.ZDL`, 32,714 B, sha256
`98968175517719d139289d69008fee0012bda59fc366b51d8b69e8ff2d3644db`).

- In the SFX category.
- 8 lanes, 150 beats, 150 fills (Random included), 22 kits, two-section
  song with Skip and middle fills, Tempo 40-240 BPM.
- Saved knob values load with patches (G1Xon).
- Kits rendered once into a cache: ~15 % DSP while playing; the cache is
  checked and re-rendered after patch switches.
- Count-in while a kit renders: the closed hi-hat renders first and counts
  quarter notes; the song starts at beat 1 on the bar after the kit is
  ready, after 1 count bar or an even number.
- No crash fill on the song's first downbeat after a kit change or patch
  switch.
