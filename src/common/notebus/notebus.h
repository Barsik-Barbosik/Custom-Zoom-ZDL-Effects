/* notebus: note events from a sequencer (MeloBox) to a sound source
 * (SndBank) in another chain slot, through the sound source's slot rows.
 *
 * Bus word = the sound source's knob 1 word in the state[31] table row
 * (column 2; zmenu.h's ZmFw tab + 44 * slot + 8). Its low NB_LOW bits are
 * knob 1's patch field: the sound source's stored data, never touched by
 * the sequencer (keep them as read). The bits above the field are never
 * saved; the sequencer writes one event there per change, and the word
 * keeps it until the next:
 *   bits 13..16  event counter 1..15 (a new value = a new event; 0 = none:
 *                no sequencer yet, or the sound source's knob 1 was turned)
 *   bits 17..23  note 0..127 (MIDI numbers, 60 = middle C); 0 = note off
 *   bits 24..28  sound 0..31 (the sound source's list; taken at a note on)
 *   bits 29..31  reserved (accent, slide), 0 for now
 * The sound source reads the word every audio call (at most one call late,
 * depending on the chain order). G1on family: tested; MS pedals: untested.
 */
#ifndef NOTEBUS_H
#define NOTEBUS_H

#define NB_COL      2u                    /* knob 1's column in the slot rows */
#define NB_LOW      13u                   /* knob 1's patch field bits (G1on) */
#define NB_LOWMASK  0x1FFFu
#define NB_CNT(w)   ((w) >> 13 & 15u)     /* event counter */
#define NB_NOTE(w)  ((w) >> 17 & 127u)    /* note, 0 = off */
#define NB_SOUND(w) ((w) >> 24 & 31u)     /* sound number */
#define NB_EVENT(cnt, note, snd) ((cnt) << 13 | (note) << 17 | (snd) << 24)

#endif /* NOTEBUS_H */
