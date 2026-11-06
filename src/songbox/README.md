# SongBox — chord song sequencer ZDL for Zoom pedals

![SongBox screen image](screen.png)

SongBox plays a chord song with bass and chords (electric piano or organ)
along with your guitar. You write the song on the pedal, bar by bar, and it can drive
DrumBox in the same patch, so the drums follow the song.

- **Songs of up to 128 bars,** each with a chord and one of 3 rhythms.
- **Chords:** the 24 major and minor chords (sharp names), Rest (silent
  bar) and END (marks the song's end).
- **3 rhythms per song** ("Rhy1" to "Rhy3"), each set to one of 29 patterns
  (bass line + chord rhythm + the DrumBox beat and fills that go with it).
- **Saved in the patch** through [Floppy](../floppy/README.md), a storage
  effect in another slot of the same patch. One Floppy holds 106 bits
  (repeated bars and sections cost little); up to 4 copies hold longer
  songs (102 bits more each). Without Floppy the song is not saved.
- **Drives DrumBox:** with [DrumBox](../drumbox/README.md) in the same
  patch, SongBox sets its beat, fills and tempo for each bar and starts and
  stops it with the song (MS pedals untested). After a patch change both
  start with 2 count-in bars while DrumBox loads its sounds.
- **Tempo 40–250 BPM.** Three instrument sets (electric piano, jazz organ,
  rock organ), each as bass + chords, bass only or chords only, or muted
  (DrumBox keeps following).
- **Song settings** (MENU): play once or in a loop, chords an octave up or
  down, a chord every bar, half bar or beat.
- **Footswitch on:** the song plays from bar 1. **Off:** it stops.
- The guitar passes through at full level, mixed with the instruments.

Version 0.7 (beta). Status: works on the B1Xon 1.30 (G1on family). Untested on the MS pedals
(MS-70CDR 2.10, MS-50G 3.10, MS-60B 2.10; their addresses are read from
the firmware, not tried): saving and DrumBox control assume the same patch
layout as on the G1on. The MS-60B has 4 slots per patch.

## Install

`SONGBOX.ZDL` (33,722 B) needs 9 free flash blocks; `FLOPPY.ZDL` 1 more.
It is in the **SFX** category (gid 7, fxid 521). Install both as described
in the [main README](../../README.md#install), then put SongBox, Floppy
and (optionally) DrumBox into one patch.

## Controls

| Page | Knob | Values | Meaning |
|---|---|---|---|
| 1 | Bar | MENU, 1–128, STATUS | Bar to edit; MENU and STATUS see below |
| 1 | Chord | END, C–B, Cm–Bm, Rest | The bar's chord |
| 1 | Rhythm | Rhy1, Rhy2, Rhy3 | The bar's rhythm |
| 2 | Rhy1 | 29 patterns ([Patterns](#patterns)) | Pattern played by rhythm 1 |
| 2 | Rhy2 | as Rhy1 | Pattern played by rhythm 2 |
| 2 | Rhy3 | as Rhy1 | Pattern played by rhythm 3 |
| 3 | Tempo | 40–250 | Tempo in BPM |
| 3 | Volume | 0–100 | Instrument level (50 = unchanged) |
| 3 | Instr | EPiano, EP Bs, EP Ch, JzzOrg, Jaz Bs, Jaz Ch, RckOrg, Rck Bs, Rck Ch, Mute | Instrument set and which parts play |

### Writing a song

1. Turn "Bar" to 1 and pick its "Chord" and "Rhythm".
2. Turn "Bar" one step further: the bar after the song's end shows END.
   - Turn "Chord" there: a new bar with that chord is added.
   - Turn "Rhythm" there: the last bar is copied with that rhythm.
3. Repeat. Bars are added one at a time; "Bar" can't go past STATUS.
4. To end the song early, set a bar's chord to END. Bars after it stay in
   memory until MENU → TRIM removes them, but only the bars before it play
   and are saved.

### MENU (Bar = MENU)

"Chord" picks the option; the knob names spell its question and "Rhythm"
gives the answer. OCTAVE and LENGTH are saved with the song; on a patch
change PLAY goes back to LOOP and OCTAVE / LENGTH to the saved values.

| Option | Question | Values |
|---|---|---|
| PLAY | Select play mode: | OFF, ONCE (stop after the last bar), LOOP (default). Not saved: LOOP again after a reload |
| OCTAVE | Shift chords by: | -1, 0 (default), +1: the chords one octave down / up (the bass stays) |
| LENGTH | Change chord every: | BAR (default), HALF, BEAT: how long each song bar plays, for songs that change chords within a bar (still 4/4, not a time signature). Rhythms and DrumBox still run in whole bars; a song that ends inside one rests up to its end |
| TRIM | Trim after END? | NO, YES: after a 5 s countdown ("YES 5" … "YES 1") removes the first END and every bar after it, then shows DONE |
| CLEAR | Clear the song? | NO, YES: after a 5 s countdown empties the song, then shows DONE |

### STATUS (the position after the last bar)

Read only. The knob names show the song's size in bits, used / capacity,
and the playing bar ("Size:" "55/106" "B: 12"; "Pack" while saving).
Without Floppy the first two take turns, one per bar (about 1.5 s each
when not playing): "Size: 55/0", "Can't save!", "(no  floppy)". The values
show the playing chord and, after an arrow, the next one ("STATUS" "Am"
"→  G").

### Messages

- **Full:** the edit doesn't fit into the Floppy copies present; the
  knobs jump back. Add another Floppy or shorten the song.
- **Flop?:** the song needs a Floppy copy that is missing; only CLEAR works
  until it is back.
- **Err:** the packed song failed to decode back to the edit three times;
  the edit is refused (please report it).

## Patterns

Each pattern is named after the DrumBox beat it plays. Defaults: Rhy1 Rock,
Rhy2 Funk, Rhy3 Balld.

| Screen | Name | Screen | Name | Screen | Name |
|---|---|---|---|---|---|
| Rock | Rock | DncPp | Dance Pop | DeepH | Deep House |
| Rock4 | Rock 4 | Motwn | Motown | Techn | Techno |
| Drive | Driving 8ths | Cntry | Country | Break | Breakbeat |
| 4Flr | Four-Floor Rock | Ska | Ska | BBap | Boom Bap |
| HTRk | Half-Time Rock | Galop | Gallop | Half | Half Time |
| Punk | Punk | Metal | Metal | DnB | Drum and Bass |
| Balld | Ballad | Funk | Funk | Rgtn | Reggaeton |
| PwrBd | Power Ballad | Funk2 | Funk 2 | Bossa | Bossa Nova |
| Pop2 | Pop 2 | Afro | Afrobeat | 1Drop | One Drop |
| PopRk | Pop Rock | Disco | Disco | | |

## How it works

- **Storage:** the song is packed into a bit stream (key + 3-bit tokens:
  OCTAVE and LENGTH, 3 bits (6 for LOW or HIGH with BEAT), then chord
  degrees, rhythm changes, repeats, section copies) and written to
  Floppy's knob fields through [zmenu](../common/zmenu/README.md) stream mode.
  The packer runs a few steps per audio call and checks that the stream
  decodes back to the song before writing it.
- **Sound:** bass and chords from one of three instrument sets, mono,
  added to the guitar. Each instrument plays one wavetable, built in
  `ctx[3]` a little per audio call after loading or a patch change.
- **DrumBox:** SongBox writes DrumBox's knob values for each bar; DrumBox
  (follow mode) copies them every call. With shorter song bars (MENU
  LENGTH) a DrumBox bar takes the beat of its first chord bar.
- **State** lives in the effect's `ctx[3]` memory.

## Files

| File | Purpose |
|---|---|
| `songbox.c` | Audio function `Fx_SFX_SongBox`: editor, packer, player, DrumBox control |
| `build.py` | Generates the headers, runs `cl6x`, then the linker |
| `instruments/` | One file pair per instrument: `<id>.py` (Python model) and `<id>.c` (its C mirror); `inst_lib.h` C helpers. Used: `epiano`, `organ_jazz`, `organ_rock`; `synth_pad`, `synth_bass` and `finger_bass` are kept for later sets |
| `inst_lib.py` | Helpers shared by the instrument models (sine, decay coefficients, voice) |
| `sets.py` | Instrument sets (one keys + one bass instrument each) and the Instr knob values → `songbox_instr.h` |
| `proto.py` | Patterns and note increments → `songbox_sound.h`; renders WAV prototypes of each set |
| `patterns/patterns.json` | The 29 patterns: bass, chords and DrumBox beat / fills (format in `song_patterns.py`) |
| `patterns/song_patterns.py` | Reads and checks `patterns.json` |
| `packer/songseq.py` | Reference model of the song stream format (packer + unpacker) |
| `packer/songpack.py` | The same, written the way the C does it (fixed arrays, integers, steps) |
| `packer/songpack.h` | Line-by-line C copy of `songpack.py`, included by `songbox.c` |
| `packer/songpack_vectors.txt` | Test vectors for the packer |
| `screen.png` | The 128x64 screen image |

Generated (edit their sources, not them): `songbox_sound.h` (`proto.py`),
`songbox_instr.h` (`sets.py` + `instruments/`),
`songbox_drums.h` (DrumBox's `patterns.py` + `patterns.json`),
`songbox_tests.h` (`songpack.py`, not used by the current build).

## Build

Needs the TI C6000 Code Generation Tools (see the
[DrumBox README](../drumbox/README.md#build)), `../common/zmenu/` and
`../drumbox/patterns/`.

```
python -B build.py --out PATH          # SONGBOX.ZDL to PATH
python -B build.py --probe --out PATH  # with the CPU load meter in STATUS (development)
python -B build.py --tests             # only regenerate songbox_tests.h (no TI needed)
python -B proto.py [--set epiano]      # instrument set prototypes -> preview/*.wav
```

After building check: `.fardata: 4 bytes`, the `.obj` relocations each
with a `.rela.dyn` entry, and the allocated size under ~29,968 B.

## Known limitations

- **MS pedals untested** (MS-70CDR 2.10, MS-50G 3.10, MS-60B 2.10): song
  storage and DrumBox control assume the G1on patch layout.
- **Mono instruments.**
- **One SongBox per patch:** a second copy stays silent (the first in the
  chain plays).
- More than one Floppy copy is untested.
