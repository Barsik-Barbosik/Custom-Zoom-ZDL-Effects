/* notebus: note events from a sequencer (MeloBox) to a sound source
 * (SndBank) in another chain slot, through the sound source's slot rows.
 * Described in README.md in this folder.
 *
 * Bus word = the sound source's knob 1 word in the state[31] table row
 * (column 2; zmenu.h's ZmFw tab + 44 * slot + 8); a second bus = its knob 2
 * word (column 3, + 12), the same layout (SndBank: bus b plays voice b).
 * Its low NB_LOW bits are the knob's patch field: the sound source's
 * stored data (see the voice settings below; keep the other bits as read).
 * The bits above the field are never saved; the sequencer writes one event
 * there per change, and the word keeps it until the next:
 *   bits 13..16  event counter 1..15 (a new value = a new event; 0 = none:
 *                no sequencer yet, or the sound source's knob was turned)
 *   bits 17..23  note 0..127 (MIDI numbers, 60 = middle C); 0 = note off
 *   bits 24..31  reserved (a per-note accent later), 0 for now
 * The sound source reads the word every audio call (at most one call late,
 * depending on the chain order). G1on family: bus 1 tested, bus 2
 * untested; MS pedals: untested.
 *
 * Voice settings (SndBank's interface, for any effect that drives it): the
 * sound source's stored knob fields read one after another (knob 1's
 * NB_LOW bits, then knob 2's, ..: bit 13 = knob 2's bit 0) hold one byte
 * per voice, voice v (1..) at bits 8 (v - 1) .. 8 v - 1: its sound value
 * = sound * NB_LEVELS + level (SndBank: sounds 0 PB, 1 Tr, 2 OJ .. 8
 * Pk, level 0..19 shown x 5, 10 = x1; above 179 = NB_SV_DEF). The driving effect
 * writes the bytes (table and record rows, keeping the bus bits) when a
 * setting changes; the sound source reads them from its record row a few
 * times a second: a new level plays at once, a new sound from the next
 * note on.
 */
#ifndef NOTEBUS_H
#define NOTEBUS_H

#define NB_COL      2u                    /* bus 1 = knob 1's column in the slot rows (bus b: + b) */
#define NB_LOW      13u                   /* knob 1 / 2's patch field bits (G1on) */
#define NB_LOWMASK  0x1FFFu
#define NB_CNT(w)   ((w) >> 13 & 15u)     /* event counter */
#define NB_NOTE(w)  ((w) >> 17 & 127u)    /* note, 0 = off */
#define NB_EVENT(cnt, note) ((cnt) << 13 | (note) << 17)
#define NB_LEVELS   20u                   /* voice settings: levels per sound */
#define NB_SV_DEF   10u                   /* sound 0 at level 10 (x1) */
/* voice settings: the first fields' bits, from knob 1's and knob 2's field
 * values (2 voices); voice v's byte = NB_SV_BYTE(that, v) */
#define NB_SV_CAT(f1, f2) (((f1) & NB_LOWMASK) | ((f2) & NB_LOWMASK) << NB_LOW)
#define NB_SV_BYTE(cat, v) ((cat) >> (8u * ((v) - 1u)) & 255u)

#endif /* NOTEBUS_H */
