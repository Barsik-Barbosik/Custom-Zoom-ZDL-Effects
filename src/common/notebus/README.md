# notebus: notes from a sequencer to a sound source

notebus carries note events from a sequencer effect to a sound source in
another chain slot: [MelodyBox](../../melodybox/README.md) (MeloBox) plays
its two tracks through [SndBank](../../sndbank/README.md). It also defines
SndBank's **voice settings**: the sound and level per voice, stored in
SndBank's own knob fields.

notebus is small and fixed: one receiver found by its effect id, two buses,
one note per bus per call. **MeloBox and SndBank now send their notes over
[midibus](../midibus/README.md)** (MIDI channels 1 and 2), which has
channels, velocity, clock and many senders and listeners. Of notebus they
still use the **voice settings**; the bus words below are no longer used.

`notebus.h` holds only the word layout (macros). The firmware table
addresses come from [zmenu](../zmenu/README.md)'s `ZmFw` table.

## Status

| | |
|---|---|
| Tested | B1Xon 1.30 (G1on family): bus 1 and bus 2 (MeloBox's two tracks, before midibus), voice settings |
| Untested | MS pedals (MS-50G, MS-60B, MS-70CDR) |
| Used by | voice settings: MeloBox (writes) and SndBank (reads), both in development, not released. Bus words: no effect now |

## The bus words

The pedal keeps one **table row** per chain slot with one 32-bit word per
knob (`ZmFw.tab + 44 * slot`; knob k at column k + 1). A bus is one knob
word of the **sound source's** row:

| Bus | Word |
|---|---|
| 1 | sound source's knob 1 (column 2, `NB_COL`) |
| 2 | sound source's knob 2 (column 3) |

SndBank plays bus b on voice b.

Bits of a bus word:

| Bits | Field |
|---|---|
| 0..12 | the knob's patch field (`NB_LOWMASK`): the sound source's stored data (voice settings below). The sender keeps these bits as it reads them |
| 13..16 | event counter 1..15 (`NB_CNT`). A new value is a new event; 0 = none (no sequencer yet, or the knob was turned) |
| 17..23 | note 0..127 (`NB_NOTE`, MIDI numbers, 60 = middle C); 0 = note off |
| 24..31 | reserved (a per-note accent later), 0 for now |

The pedal saves only the patch field, so the event bits never reach the
patch.

### Sender (MeloBox)

- Finds the sound source by its **effect id** in the slots' record rows
  (`ZmFw.rec + 44 * slot + 4`; SndBank = gid 7, fxid 531) when it looks at
  the chain: the first SndBank after it, up to the next effect that saves
  through Floppy (the chain wraps from the last slot to the first).
- Per event, checks first that the slot still holds that id. An effect
  deleted before it shifts the chain within the same step, and the old
  slot then holds another effect, which must not get the write. If the id
  is gone, the event is dropped; MeloBox plays its own simple voices until
  its next step start finds SndBank again (also with no SndBank at all).
- Writes `(word & NB_LOWMASK) | NB_EVENT(cnt, note)`: counter + 1, wrapping
  15 -> 1 (never 0). One event per bus per audio call; a note on replaces
  the last note.

### Receiver (SndBank)

- Every audio call reads both bus words from its own table row (at most
  one call late, by chain order).
- A counter other than the last one seen is a new event: the note on (new
  pitch, the voice's current sound) or note off (0).
- Locks its knobs: an edit handler puts the stored value back after a
  physical turn (a turn writes a plain value, so the counter reads 0 and
  no event fires) and restores the bus bits.

## Voice settings

The sound source's stored knob fields, read one after another, hold **one
byte per voice**: knob 1's 13 bits first, then knob 2's (bit 13 = knob 2's
bit 0). Voice v (1, 2) is bits 8(v-1) .. 8v-1:

```
value = sound * 20 + level         (NB_LEVELS = 20)
cat   = NB_SV_CAT(knob1, knob2)    (both fields, 26 bits)
byte  = NB_SV_BYTE(cat, v)
```

| Field | Values (SndBank) |
|---|---|
| sound | 0 PB, 1 Tr, 2 OJ, 3 OR, 4 EP, 5 Mr, 6 Kl, 7 Sy, 8 Pk |
| level | 0..19, shown x 5; 10 = x1 |
| default | 180..255 (no such sound): `NB_SV_DEF` = sound 0 at level 10 |

The driving effect writes the bytes into the sound source's table **and**
record rows (keeping the bus bits in the table row) when a setting changes.
The sound source reads its record row every 64 calls (~12 ms): a new level
plays at once, a new sound from the next note on.

## Cost

Measured on the B1Xon 1.30 with a probe effect doing the same work
(cycles per audio call, mean; one call is ~32,650 cycles):

| Part | Cycles | % of a call |
|---|---|---|
| receiver (both bus words every call) | ~130 | ~0.4 % |
| sender (id check + read-modify-write per event) | ~50 | ~0.15 % |
