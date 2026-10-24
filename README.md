# Custom Zoom ZDL effects

Custom effects (ZDL) for Zoom pedals of the MultiStomp / G1on / B1on family.

| Effect | Category | What it is |
|---|---|---|
| [DrumBox](#drumbox) | SFX | Drum machine: 150 beats, 150 fills, 22 kits, songs of two sections |

More effects will follow.

## DrumBox

![DrumBox on the pedal's screen](docs/images/screen.png)

DrumBox is a full drum machine that plays along with your guitar,
started and stopped with the footswitch.

- **8 drum lanes:** kick, snare, closed and open hi-hat, ride, crash,
  high and low tom.
- **150 beats:** rock, pop, punk, metal, funk, breaks, house, hip-hop, latin,
  reggae, jazz, blues, ... and Random (a new randomly chosen beat every time
  its section starts), plus None (no beat, only the fills play).
- **150 fills:** snare, tom, kick and mixed fills of ½, 1, 2 and 4 beats,
  stops, breaks, ... and Random (a different randomly chosen fill each time,
  never a stop or break), plus None (no fill). Crash fills land a crash
  on the next downbeat. Together: over 22,000 possible beat + fill combinations.
- **A song of two sections** (verse / chorus): "Beat A" for "Length A" bars
  with "Fill A", then "Beat B" for "Length B" bars with "Fill B", 1 to 64 bars each, optionally
  with a fill in the middle too, or Skip.
- **22 kits:** sampled acoustic kits (drums resynthesized from WAV samples),
  synth, electronic, 8-bit and lo-fi kits, and mixes of them.
- **Tempo 40–240 BPM.** The screen shows beat, fill, length and kit names
  and the BPM.
- The guitar passes through at full level, mixed with the drums.

### Pedals

These pedals use the ZDL effect format (`.ZDL` files):

| Pedal | Firmware | DrumBox |
|---|---|---|
| Zoom G1Xon | 1.21 | **Works** (all features tested) |
| Zoom G1on | — | Untested |
| Zoom B1on | — | Untested |
| Zoom B1Xon | — | Untested |
| Zoom MS-70CDR | 2.10 | Untested; the toolchain's main target (other custom ZDLs run on it) |
| Zoom MS-50G | — | Untested |
| Zoom MS-60B | — | Untested |

Not supported: the newer pedals that use `.ZD2` effects instead of ZDL
(MS-50G+, MS-60B+, MS-70CDR+, G1Four / G1XFour, B1Four / B1XFour,
A1Four / A1XFour, G3n / G3Xn, G5n, B3n, ...).

Reports for other pedals are welcome. Please open an issue with the
pedal, firmware and what happened.

> **Use at your own risk.** Custom effects run code on the pedal's DSP. A
> bad effect can freeze the pedal when it is selected; power-cycle it and
> delete the effect. Keep a backup of your pedal's effects before
> installing anything.
> This project is not affiliated with or endorsed by Zoom.

### Install

Download `DrumBox.ZDL` from the [Releases](../../releases) page (or use
`dist/DrumBox.ZDL`). The effect needs 8 free flash blocks (32,714 B).

Install it like any other effect with
[Zoom Effect Manager 2](https://zoomeffectmanager.com/en/): add the
ZDL to its custom folder and write it to the pedal. Or you can still use
my old Zoom Firmware Editor: add the ZDL to the pedal's firmware updater
file and run the updater.

DrumBox is in the **SFX** category.

### Knobs

| Page | Knob | Meaning |
|---|---|---|
| 1 | BeatA | Section A (verse) beat (default Rock4) |
| 1 | FillA | Fill on section A's last bar (default Sn1) |
| 1 | LenA | Section A length: Skip, 1Bar … 64Bar; "M" = fill in the middle too (default 8BarM) |
| 2 | BeatB | Section B (chorus) beat (default Funk) |
| 2 | FillB | Fill on section B's last bar (default Randm) |
| 2 | LenB | Section B length (default 8BarM) |
| 3 | Tempo | 40–240 BPM (default 120) |
| 3 | Volume | Drum level |
| 3 | Kit | Drum kit (default Ac1) |

After a kit change the kit is rendered first (a few seconds). The closed
hi-hat is ready almost at once and counts quarter notes meanwhile; the song
starts again at beat 1 on the bar after the kit is ready (after 1 count bar
or an even number).

All beats, fills and kits are listed in **[drumbox/README.md](drumbox/README.md)**.

## Building

You only need this to change an effect.

- **TI C6000 Code Generation Tools:** the standalone 8.3.15
  ([ti.com/tool/C6000-CGT](https://www.ti.com/tool/C6000-CGT)) or 8.5.0.LTS
  from Code Composer Studio 20.x. `build.py` looks at `TI_CGT_ROOT`, then at
  `C:/ti/...`.
- **Python 3.8+** with Pillow; numpy only for fitting new drum samples.

```
pip install -r requirements.txt
cd drumbox
python -B build.py --headers   # regenerate the generated headers (no TI needed)
python -B build.py             # full build -> ../dist/DrumBox.ZDL
python -B preview.py --beat-a Rock4 --kit 0   # render a WAV on the desktop (Python mirror of the DSP)
```

The build is reproducible: the released ZDL comes out byte for byte the same.

| Path | What |
|---|---|
| `drumbox/` | DrumBox: DSP (`drumbox.c`), beats and fills (`patterns.py`), kits (`kits.py`), drums (`drums/`), build and preview scripts |
| `build/` | The linker that turns the TI object file into a ZDL, based on repeat98's, with additions (see its header) |
| `tools/` | Experimental G1Xon USB file transfer, used during development (not for general use yet) |
| `docs/HARDWARE-NOTES.md` | What was learned on the hardware: load limit, `_init`, knob ranges, DSP load, … |
| `dist/` | The current builds of the effects |

## Inspiration and documentation

These effects stand on the work of these projects:

- [repeat98/ZoomMultistompZDL](https://github.com/repeat98/ZoomMultistompZDL):
  the reverse-engineered ZDL ABI and the Python linker this build is based on.
- [themanro/ZoomMultistompZDL](https://github.com/themanro/ZoomMultistompZDL):
  the materializing `_init` (saved knob values on patch load), effects and a
  patch editor.
- [ELynx/zoom-fx-modding](https://github.com/ELynx/zoom-fx-modding): the
  first public documentation of the ZDL format, screen images and the DSP
  buffer layout.
- [ELynx/zoom-fx-compressed](https://github.com/ELynx/zoom-fx-compressed):
  stripped stock effects that free flash space.
- [matujuice/zoom-ms-zdl-effects-pack](https://github.com/matujuice/zoom-ms-zdl-effects-pack)
  and [matujuice/zoom-ms-midistomp](https://github.com/matujuice/zoom-ms-midistomp):
  synth-oriented effects and MIDI clock firmware for MS pedals.
- [Leemuzhko/HYBRID-IR](https://github.com/Leemuzhko/HYBRID-IR) and
  [Leemuzhko/Zoom_FX_Project](https://github.com/Leemuzhko/Zoom_FX_Project):
  cabinet IR effects without the TI compiler, knob display callbacks.
- [zisonzishen0415-stack/b1on-firmware-reverse](https://github.com/zisonzishen0415-stack/b1on-firmware-reverse):
  G1on / B1on firmware reverse engineering, including the DSP cost budget.
- [shooking/ZoomPedalFun](https://github.com/shooking/ZoomPedalFun): SysEx
  tips and tools for many Zoom pedals.
- [mungewell/zoom-zt2](https://github.com/mungewell/zoom-zt2): effect list and
  file transfer for the newer Zoom pedals.
- [thammer/stomphacks](https://github.com/thammer/stomphacks): custom
  effects for the MS Plus pedals.

## Licence

MIT, see [LICENSE](LICENSE). Third-party code and the LineSel handler blob
are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
