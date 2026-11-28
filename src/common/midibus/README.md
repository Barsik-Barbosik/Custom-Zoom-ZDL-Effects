# midibus: MIDI between ZDL effects

midibus lets ZDL effects in one patch chain send MIDI messages to each
other: notes, controllers, clock. An effect can send, receive or do both.
Messages are broadcast: every receiver in the chain hears every sender, and
the receiver filters by channel. Effects do not need to know each other's
ids, and no knob is used for the transport.

This page is the contract: the port block layout and the rules below.
`midibus.h` in this folder is one implementation in C (MIT). An effect
built with another toolchain can join if it follows the same layout and
rules.

Traffic stays inside the chain. midibus does not reach the pedal's USB or
DIN MIDI.

## Status

| | |
|---|---|
| Port version | 2 |
| Tested | B1Xon 1.30 (G1on family), with probe effects: order, drops, bursts, running status, channel filter, merging two senders, both chain orders, and deleting, replacing and moving effects while they send |
| Untested | the MS pedals (MS-50G, MS-60B, MS-70CDR). Their firmware builds the slot descriptors the same way (see below), so it should work there, but it has not run on one |
| Used by | MelodyBox (sends its two tracks on channels 1 and 2) and SndBank (plays them), both in development, not released; tested on the B1Xon including deleting and moving MelodyBox while it plays |

## How an effect finds the other slots

The pedal gives every chain slot a memory area. `ctx[3]` points at the own
slot's **descriptor**: three 32-bit words, base, end and length of the
area. The descriptors of all slots lie **12 bytes apart in slot order**,
and `ctx[0]` is the own slot number (0 = first), so:

```
descriptor of slot s = ctx[3] + 12 * (s - ctx[0])
```

This was checked on the B1Xon 1.30 and found in the firmware of the G1Xon
1.21, MS-50G 3.10, MS-60B 2.10 and MS-70CDR 2.10. The pedals have 4 to 6
slots. Scan slots 0..5 and check each descriptor before using it: past the
last slot the same address holds other OS data. The C library accepts a
descriptor only if it lies in 0xC0000000..0xC7FFFFF0, is 4-byte aligned,
and describes an area in DDR (0xC0000000..0xC8000000, end > base) of 4 KB
to 2 MB.

What was seen on the B1Xon:

- Every slot has a descriptor, an empty slot too. The areas are back to
  back, 705,600 B each.
- The areas belong to the **slots**, not to the effects. An effect moved to
  another slot gets that slot's area.
- Nothing clears an area on a patch switch, a replace or a move. Old port
  blocks stay behind with a frozen heartbeat. Stock effects (delays, for
  example) write audio data up to their area's end.
- An effect switched off with its footswitch is still called every audio
  call.

## Port block (version 2)

A sender keeps one port block in the **last 128 bytes of its own area**:
`port = (end & ~7) - 128`. All words are 32-bit little-endian.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| +0 | 4 | magic | 0x4944494D ("MIDI" in memory) |
| +4 | 4 | info | bits 0-7 version (2), bits 8-15 flags (bit 0 = sends, bit 1 = receives), bits 16-23 own slot number |
| +8 | 4 | heartbeat | +1 every audio call |
| +12 | 4 | count | bytes ever written (wraps at 2^32) |
| +16 | 4 | seal | count XOR epoch, written after count |
| +20 | 64 | ring | raw MIDI bytes, byte i at ring[i & 63] |
| +84 | 44 | reserved | write 0, ignore on read |

The host effect must not use these 128 bytes for anything else.

## Sender rules

1. **Set up the port** when the block is not yours: magic or info differ
   (fresh area, another effect's leftovers, other flags, a move to another
   slot). Check this when your state is set up, after `ctx[3]` changes, and
   again every 16 calls or so.
   - New epoch = (old count XOR old seal) + 0x9E3779B9. This takes what
     the block held before, so the new epoch is never the old one.
   - Write in this order: magic = 0, info, count = 0, seal = epoch,
     heartbeat = 0, then **magic last**.
2. **Every call**, footswitch on or off: heartbeat + 1.
3. **Write a message**: the bytes into the ring, then count, then seal =
   count XOR epoch. A reader that sees the new count also sees the bytes.
4. **Budget: at most 64 bytes per call.** A receiver earlier in the chain
   reads one call later, so it would lose anything more. Keep the rest for
   the next call.
5. **Stop when the slot is being replaced.** Once the host knows its slot
   is no longer its own (for example its record in the firmware tables
   changed), it must not write to the port any more: the area now belongs
   to the next effect.

## Receiver rules

1. **Scan** the other slots every 16 calls; skip your own slot. A port
   **counts** when:
   - the descriptor passes the checks above;
   - magic, version 2, the send flag and the slot field (= s) all match;
   - its address and epoch (count XOR seal) are the same as at the last
     scan;
   - its heartbeat moved at **two scans in a row**. A frozen heartbeat is a
     stale block from a removed effect.

   Start reading a new port at its current count: never replay old bytes.
2. **Drop a port** at the first scan where its heartbeat stood still, or
   at once (every call) when count XOR seal is not its epoch any more. A
   new epoch means a new sender (read it from its count once it counts
   again). A wrong seal also catches another effect writing over the
   block.
3. **Every call**, for each live port: read count and seal. If count minus
   your read position is more than 64, the ring was overwritten before you
   read it. Count the lost bytes, jump to count - 64 and wait for the next
   status byte.
4. **Parse** each port on its own: running status, real-time bytes anywhere.
   Merge the ports in slot order.
5. **Release notes** from a port that stops counting (removed, stopped,
   replaced, a new epoch). Otherwise its notes hang.
6. Call the receiver **before** the footswitch check, so a switched-off
   receiver keeps its read positions up to date.

Latency: a receiver after the sender in the chain hears a message in the
same call; a receiver before it hears it one call later (~0.18 ms).

## Messages

| Bytes | Handling |
|---|---|
| 0x80..0xEF channel messages | passed, running status accepted, filtered by the receiver's channel mask |
| 0xF8..0xFF real-time (clock, start, stop, ...) | passed, anywhere in the stream, never filtered |
| 0xF0..0xF7 system common and SysEx | not sent by `mb_out_msg`; skipped by the receiver (they also end running status) |

## Host effect duties

- Keep the last 128 bytes of the own area free for the port block.
- Call the library every audio call, before the footswitch check.
- Keep `MbOut` / `MbIn` in the host's own state, zeroed when the state is
  set up. Align them to 64 bytes if possible: the state is in DDR and every
  cache line touched costs cycles.
- **State adoption:** an effect moved into a slot whose last occupant was
  the same effect type finds that effect's state still in the area, magic
  and all, and keeps it. That was harmless in the tests, but a host must
  either accept a sibling's state or tag its state with its slot.

## C library

```c
#include "midibus.h"

/* in the host's state (ctx[3] area), zeroed at set-up */
MbOut out;
MbIn in;                       /* in.chmask: bit c = channel c + 1, MB_ALLCH = all */

/* every audio call, before the footswitch check */
MbOut *o = &sp->out;           /* plain pointers in locals, see below */
MbIn *i = &sp->in;
if (mb_out_tick(o, ctx, MB_F_OUT))
    mb_out_msg(o, 0x90 | ch, note, vel);     /* 0 = no room this call */

mb_in_poll(i, ctx);
gone = mb_in_gone(i);          /* bit s: release the notes from slot s */
while (mb_in_next(i, &st, &d1, &d2)) {
    /* i->from = the sender's slot */
}
```

| Call | What |
|---|---|
| `mb_out_tick(&out, ctx, flags)` | once per call before any send: port set-up / check, heartbeat; returns the port or 0 |
| `mb_out_msg(&out, st, d1, d2)` | one whole message (1, 2 or 3 bytes by status); 1 = written |
| `mb_out_raw(&out, b)` | one raw byte (for running status) |
| `mb_out_room(&out)` | bytes left in this call's budget |
| `mb_in_poll(&in, ctx)` | once per call: scan, liveness, overflow |
| `mb_in_gone(&in)` | slots whose port stopped counting since the last call; clears them |
| `mb_in_next(&in, &st, &d1, &d2)` | next message of this call (1) or none (0); `in.from` = slot |

The functions are force-inlined, use no stack arrays, divides or globals
(the ZDL code rules). **Pitfall (TI CGT 8.3):** pass `MbOut` / `MbIn`
pointers held in locals. With a cast at the call (for example dropping
`volatile` from the host's state) the compiler left the functions out of
line in `.text`, called from `.audio`. Check the disassembly for calls and
`ADDKPC`.

## Cost

Measured on the B1Xon 1.30 (cycles per audio call, mean; one call is
~32,650 cycles at 180 MHz):

| Part | Cycles | % of a call |
|---|---|---|
| receiver, one live sender (poll + next) | ~230 | ~0.7 % |
| receiver, no sender | ~160 | ~0.5 % |
| sender (with the test effect's send loop) | ~145 | ~0.45 % |

The cost is mostly memory access to the state and the ports in DDR, not
instructions.
