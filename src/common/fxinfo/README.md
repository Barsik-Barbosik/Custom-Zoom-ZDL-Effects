# fxinfo: what an effect offers to the chain

fxinfo lets a ZDL effect tell the other effects in the same patch chain
what it offers. A sound source, for example, can publish the names of its
sounds, so a sequencer can show them without keeping its own copy. The
reader does not need to know the writer's effect id.

fxinfo is separate from [midibus](../midibus/README.md). An effect can
publish info without using MIDI, and use MIDI without publishing info. The
two only share the memory layout: the info block lies just below the place
of midibus' port block.

This page is the contract: the block layout and the rules below.
`fxinfo.h` in this folder is one implementation in C (MIT).

## Status

| | |
|---|---|
| Version | 1 |
| Tested | not yet on a pedal |
| Used by | SndBank (publishes its sounds and channels) and MelodyBox (shows SndBank's sound names), both in development |

## Where the block is

Every chain slot has a memory area. `ctx[3]` points at the own slot's
descriptor (base, end, length); slot s's descriptor = own `ctx[3]` +
12 × (s − own slot). See [midibus](../midibus/README.md#how-an-effect-finds-the-other-slots)
for the details and the checks.

With `end` rounded down to 8:

| Bytes | What |
|---|---|
| `end - 128` .. `end` | midibus' port place (used or not) |
| `end - 144` .. `end - 128` | info header (16 bytes) |
| below the header | the record (its length in the header) |

The header and the record together take at most 256 bytes. A publishing
effect keeps the last 384 bytes of its area free.

## Header

All words are 32-bit little-endian.

| Offset | Field | Meaning |
|---|---|---|
| +0 | magic | 0x4F464E49 ("INFO" in memory), written last |
| +4 | meta | bits 0-7 version (1), bits 8-15 own slot number, bits 16-31 record length in bytes (a multiple of 4, at most 240) |
| +8 | sum | meta + all record words (wrapping 32-bit sum) |
| +12 | heartbeat | +1 at every publish |

## Record

Items one after another. Each item is a word, type (bits 0-7) | count
(bits 8-15) | width (bits 16-23), followed by count entries of width bytes,
padded to 4 bytes at the item's end. Readers skip types they don't know.

| Type | Name | Entries |
|---|---|---|
| 0 | end | ends the record early |
| 1 | sounds | width 4, one per sound in program order (sound 0, 1, ..): byte 0 kind (0 melodic, 1 drum kit), bytes 1-3 the name (ASCII 0x20-0x7E, NUL padded) |
| 2 | channels | count 1, width 4: bit c = MIDI channel c + 1 is played |

Planned: the names of a drum kit's notes (a sequencer shows them instead of
note names).

## Writer rules

1. Publish at least every 128 audio calls: write the whole block when it is
   not this record (fresh area, moved, overwritten: magic 0, record, meta,
   sum, magic last), else only count the heartbeat.
2. Stop when the own slot is being replaced (the own record no longer the
   own effect's): the area then belongs to the next effect.

## Reader rules

1. Look at least 256 audio calls apart (so a live writer has published in
   between). Skip the own slot.
2. A block counts when its magic, version, slot field (= the slot it was
   found in), length and sum are right **and** its heartbeat moved since
   the previous look. Blocks of removed effects stay in memory with a
   frozen heartbeat.
3. Copy what you need into your own state. The other area may change at
   any time.

## C library

```c
#include "fxinfo.h"

/* writer: a static const record */
static const uint32_t info[] = {
    FI_ITEM(FI_SOUNDS, 2u, 4u),
    FI_ENTRY4(FI_K_MELODIC, 'P', 'B', 0), FI_ENTRY4(FI_K_MELODIC, 'E', 'P', 0),
    FI_ITEM(FI_CHANNELS, 1u, 4u), 0x3u,
};
fi_publish(ctx, info, 5u);                     /* every <= 128 calls */

/* reader: every >= 256 calls, per other slot s */
a = fi_at_slot((uint32_t)ctx[3], own, s);      /* header address or 0 */
if (a && ((const volatile FiHdr *)a)->hb != last_hb[s])
    p = fi_item(a, FI_SOUNDS);                 /* item word address or 0 */
```

The functions are force-inlined, use no stack arrays, divides or globals
(the ZDL code rules). Call the reader from cold code (`.text`): it is a few
hundred bytes.
