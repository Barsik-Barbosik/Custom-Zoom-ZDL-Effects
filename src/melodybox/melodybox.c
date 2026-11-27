/*
 * MelodyBox (MeloBox): a note sequencer / player, in development.
 *
 * Two tracks of steps. Knob 1 picks the mode: MENU (value 0), TRK1 (1,
 * default) or TRK2 (2): edit that track's steps.
 *
 * TRK1 / TRK2: knobs 1-3 show three neighbouring items of the track's
 * list: "" (empty), the steps 1..n (n = the LENGTH setting: 16, 32, 64 or
 * 128, both tracks), "END"; both tracks share the scroll offset. A step's
 * name is its number (2 digits; after 99 again 00, 01, ..; knob 2's text
 * shows glyph 0x1D + "_" instead on steps 1, 5, 9, ..) plus its value:
 * "01___" rest, "01Hld" hold (the note before goes on), "01_C2" a natural
 * note, "01C#2" a sharp one (octaves 2..5: C2..C5). Knob 3 scrolls: its
 * value is the scroll offset, so turning it right moves the three knobs one
 * item on. Knob 2 edits the value of the step it shows (0 rest, 1 hold,
 * 2..38 = C2..C5); on a scroll it jumps to the new step's value. Knob
 * texts: knob 1 "TRK1" / "TRK2", knob 2 its own name, knob 3 "SCRLL".
 *
 * MENU (as SongBox's): knob 2 picks the option (text TEMPO, LENGTH, GRID,
 * SOUND1, SOUND2), the knob names spell its question, knob 3 gives the
 * answer: TEMPO Pedal (0, default: the pedal's tempo) or 40..250 BPM (39 +
 * value), LENGTH 16 / 32 / 64 / 128 steps and GRID 1/16 or 1/8 (a step's
 * length in playback) for both tracks, SOUND1 / SOUND2 the sound source's
 * sound and its level for track 1 / 2 (value = sound * 20 + level, as
 * SongBox's Bass / Keys knobs; text "EP:70": sound PB .. Pk, level x 5,
 * 00 = silent, 50 = x1, default PB:50). Back on a track, knobs 2 and 3 jump
 * back to the step and its value.
 *
 * Saving (as SongBox's): the steps and settings are saved in the patch
 * after each change, packed with packer/melopack.h into a storage chain:
 * part 0 = MelodyBox's own knobs 4-9, then the storage effects in its
 * owner range (the slots after it up to the next storage user, wrapping, as
 * zmenu's ZM_S_RANGE): SndBank first (it also keeps SOUND1 / SOUND2 in the
 * first 16 bits of its part), then the Floppies in their part marker order
 * (bits 0..3 of a Floppy's part: MB_FL_MARK + its place 1..4; an unmarked
 * Floppy gets the next marker). MB_PK_WAIT calls after the last edit the
 * packer starts (a few units per call, MB_PK_CYC TSCL cycles); its result
 * is decoded again and compared with the grids, then all parts are written
 * in one call. A melody that doesn't fit the chain (or doesn't decode
 * back) is refused: the grids and TEMPO / LENGTH / GRID go back to the last
 * saved ones, the knobs jump there and knob 2's name shows FULL for MB_FULL
 * calls. A SOUND change is written straight into SndBank's part. Loads:
 * the chain is polled every MB_POLL calls; a change counts once two polls
 * agree and the storage rows equal their records (after a patch change the
 * OS fills the rows late; Floppy markers are written only once loaded and
 * settled). Not loaded yet (a fresh state, back after a gap of MB_AWAY_CYC
 * in the audio calls, another patch number) or the same chain with other
 * words (another writer): the chain is decoded into the grids. The chain
 * changed within MB_SETTLE polls of a load (a part late) or together with
 * the own part (a whole patch sent): loaded again. Until loaded and within
 * MB_SETTLE polls of a load, knob 2 / 3 moves are no edits. The chain itself changed
 * (a storage effect added or removed): the melody in RAM is
 * packed into it; until a save (or load) succeeds a melody that doesn't
 * fit is not refused (over: the edits stay, FULL shows, nothing is
 * written), so it can be made smaller step by step. Unknown firmware:
 * nothing is loaded or saved.
 *
 * Playback (footswitch on): the n steps of both tracks in a loop, a step =
 * a 16th or an 8th at the TEMPO setting: Pedal = the pedal tempo (the
 * firmware's global BPM, 120 if unknown). A note goes as a note on (pitch
 * p = MIDI MB_BASE + p, the track's sound) over the note bus (notebus.h) to the sound source SndBank
 * (MB_SYN_ID) in the chain, track 1 on bus 1, track 2 on bus 2 (one voice
 * each), a note off at MB_GATE of its last step (holds after it make it
 * longer); a rest is silent. SndBank's slot is looked for at every step
 * start; footswitch off: note offs, stop. A patch's first load starts the
 * loop again at step 1 (it may start playing before the melody is loaded).
 * A DrumBox in the chain (known firmware, no SongBox leading) plays its own
 * song on our clock (mb_drums): our TEMPO, stopped while we are, started
 * with our step 1 (or at our next bar line), its 16ths put on ours at each
 * quarter.
 * A SongBox in the chain with a song (RAM or Floppy) is the master: we play
 * on its clock (its Tempo table word, songbox.c SS_CK_SYNC): while it plays,
 * at its position (16ths from its song's bar 1, wrapped at our length), so
 * its tempo and swing; our TEMPO and the footswitch's start / stop aren't
 * used (footswitch off mutes). SongBox drives DrumBox then; with an empty
 * song we lead, as without SongBox.
 *
 * Own voices: with no SndBank in the chain (or an unknown firmware) each
 * track plays on one of MelodyBox's own simple voices (the notebus probe's:
 * a sawtooth through a one-pole low-pass, fast attack, slow fall while
 * held, short release), added to both ctx[5] channels; of the SOUND
 * settings only the level is used (taken at a note on).
 *
 * The names are the descriptor entries of knobs 1-3, rewritten by the audio
 * callback every call (the OS may restore them). When a name changed, the
 * screen is redrawn (3 Event_post calls, ZM_POST_GAP calls apart, only while
 * this effect is on screen and the firmware is known: zmenu.h's table).
 * The knob maxes (descriptor +0x0C) follow the mode. Jumps write the own
 * patch columns, so they need a known firmware (else a knob keeps its
 * position and a turn sets the value from there). Audio passes through
 * (plus the own voices, see above).
 *
 * Knobs 4-9 are storage (the first part of the saved chain, 8+8+8+8+27+8
 * bits): names Data1..Data6, texts DON'T / TURN / THESE (on both knob
 * pages), locked as
 * SndBank's: a turn is undone by the edit handler (Mb_LockEdit) from a
 * shadow; a record value that differs from the shadow and was the same at
 * the previous look (MB_LOOK calls apart) is another writer's (zmenu) and
 * goes into the shadow. The shadow is first taken MB_LWAIT looks after a
 * fresh state or a slot change; until then turns pass.
 */

#include <stdint.h>

#define ZM_STREAM      1
#define ZM_DESC        _Fx_SFX_MeloBox_Desc
#define ZM_MAGIC       0x4D425831u      /* "MBX1" */
#define ZM_FLOPPY_FXID 530u             /* Floppy: storage parts after SndBank */
#define ZM_S_RANGE     1                /* own only the Floppies up to the next Floppy user */
#include "zmenu.h"
#include "notebus.h"
#include "melopack.h"
#include <c6x.h>                        /* TSCL */
#ifdef MB_SELFTEST
#include "melodybox_selftest.h"         /* generated by build.py --selftest */
#endif

#pragma CODE_SECTION(Fx_SFX_MeloBox, ".audio")

#ifdef MB_SELFTEST
#define MB_MAGIC  0x4D425439u           /* "MBT9": the self-test build's layout */
#define MB_TUNITS 1u                    /* self-test: packer units per call */
#else
#define MB_MAGIC  0x4D425844u           /* "MBXD"; new value on any state layout change */
#endif
#define MB_NKNOB  3u                    /* knobs showing items */
#define MB_MODE   0u                    /* knob 1 (0-based): MENU / TRK1 / TRK2 */
#define MB_EDIT   1u                    /* knob 2: the step's value / MENU option */
#define MB_SCRLL   2u                    /* knob 3: the scroll offset / MENU answer */
#define MB_NTRK   2u                    /* tracks */
#define MB_MAXN   128u                  /* most steps */
#define MB_MAXK3  211u                  /* knob 3 build max (run time: n - 1 / the option's max) */
#define MB_MAXVAL 38u                   /* knob 2 build max: 0 rest, 1 hold, 2..38 notes */
#define MB_REST   0u
#define MB_HOLD   1u
#define MB_NOPT   5u                    /* MENU options: */
#define MB_O_TEMPO 0u                   /* TEMPO: 0 Pedal, else 39 + value BPM (40..250) */
#define MB_O_LEN  1u                    /* LENGTH: n = 16 << value (both tracks) */
#define MB_O_GRID 2u                    /* GRID: 0 = 1/16, 1 = 1/8 (both tracks) */
#define MB_O_SND  3u                    /* SOUND1, SOUND2: track 1 / 2's bus sound value */
#define MB_LEVELS NB_LEVELS             /* sound value = sound * MB_LEVELS + level (as SongBox's Bass) */
#define MB_SND_DEF NB_SV_DEF            /* PB at level 10 (shown 50, x1) */
#define MB_OMAX(o) ((o) == MB_O_TEMPO ? MP_TEMPO_MAX : (o) == MB_O_LEN ? 3u : (o) == MB_O_GRID ? 1u \
                    : MB_NSND * MB_LEVELS - 1u)
#define MB_NSND   9u                    /* SndBank's sounds (PB, Tr, OJ .. Pk) */
#define MB_SV(st) ((st)->ov[MB_O_SND] | (st)->ov[MB_O_SND + 1u] << 8)  /* SndBank's voice settings (notebus.h) */
#define MB_SYN_ID ((7u << 24) | 531u)   /* sound source: SndBank (SFX 531) */
#define MB_FXID   519u                  /* own effect id (mirrors FXID in build.py) */
#define MB_DB_ID  ((7u << 24) | 520u)   /* DrumBox (SFX 520): follows the playback */
#define MB_SB_ID  ((7u << 24) | 521u)   /* SongBox (SFX 521): drives DrumBox itself */
#define MB_DB_TEMPO 8u                  /* DrumBox's Tempo word in its row (2 + knob index) */
#define MB_DB_SYNC  0x100u              /* its sync bits (drumbox.c DB_SYNC ..): synced, */
#define MB_DB_PLAY  0x200u              /* playing, the quarter of our bar (2 bits), */
#define MB_DB_QSH   10u
#define MB_DB_T     0x1000u             /* flipped at each quarter / poll while stopped */
#define MB_DB_R     0x2000u             /* flipped at each start at step 1: DrumBox starts its song again */
#define MB_DB_READY 0x4000u             /* set by DrumBox: kit rendered (a start waits for it) */
#define MB_DB_WAIT  65536u              /* calls a start waits for it at most (~11.9 s; an older DrumBox) */
#define MB_SB_COL   8u                  /* SongBox's Tempo word in its row (2 + knob index 6): its clock */
#define MB_SB_SYNC  0x100u              /* songbox.c SS_CK_*: a song can play, */
#define MB_SB_PLAY  0x200u              /* it plays (not in the count-in), */
#define MB_SB_SSH   10u                 /* the 16th of the drum bar (4 bits), */
#define MB_SB_BSH   14u                 /* the drum bar from the song's bar 1 (7 bits) */
#define MB_SB_HOLD  64u                 /* calls the last clock word stays (a Tempo turn writes the plain value) */
#define MB_BASE   36u                   /* MIDI note of pitch 0 (C2: a bass octave) */
#define MB_GATE   0.75f                 /* note off at this part of the step */
#define MB_PH_PER_BPM 1.2093726e-05f    /* 16ths per audio call (8 samples) per BPM */
#define MB_G1_BPM 0x1181BB2Cu           /* G1on family: the global BPM int */
#define MB_NOSLOT 0xFFu
#define MB_SCOL   5u                    /* storage knobs 4-9: patch columns 5..10 */
#define MB_NS     6u
#define MB_LOOK   64u                   /* calls between lock looks (~12 ms) */
#define MB_LWAIT  16u                   /* looks before the shadow is taken (~190 ms) */
#define MB_V_ATT  0.25f                 /* own voices: attack, env += (tgt - env) MB_V_ATT per call */
#define MB_V_FALL 0.9998f               /* held note: tgt *= MB_V_FALL per call (~0.9 s) */
#define MB_V_REL  0.97f                 /* note off: env *= MB_V_REL per call (~6 ms) */
#define MB_V_LP   0.25f                 /* low-pass: lp += (saw - lp) MB_V_LP (~1.7 kHz) */
#define MB_V_GAIN 0.4f                  /* at level 10 (x1) */
#define MB_POLL     256u                /* calls between storage polls (~46 ms) */
#define MB_PK_WAIT  2048u               /* calls after the last edit before packing (~0.37 s) */
#define MB_PK_CYC   1000u               /* packer TSCL cycles per call (at least one unit) */
#define MB_PK_MAX   8u                  /* most packer units per call (also when TSCL is stopped) */
#define MB_FULL     8192u               /* calls knob 2's name shows FULL (~1.5 s) */
#define MB_AWAY_CYC 9000000u            /* a TSCL gap between calls that means away (~50 ms) */
#define MB_SETTLE   32u                 /* polls after a load: a chain change loads again (~1.5 s) */
#define MB_FL_MARK  11u                 /* Floppy part marker: MB_FL_MARK + its place 1..4 */
#define MB_NW       (4u * MP_MAXPARTS)  /* storage words */

/* State at the ctx[3] arena's end, magic at both ends. */
typedef struct {
    uint32_t magic0;
    uint32_t posts, postWait;           /* redraws left, calls until the next */
    uint32_t mode;                      /* 0 MENU, 1 TRK1, 2 TRK2 (3: jump to MENU's values) */
    uint32_t off, edit, k3;             /* scroll offset; knob 2 / knob 3 values as last seen */
    uint32_t opt, ov[MB_NOPT];          /* MENU option shown; values: tempo, length code, grid, sounds */
    uint8_t  grid[MB_NTRK][MB_MAXN];    /* step values per track: 0 rest, 1 hold, 2 + pitch */
    uint32_t play, step, len;           /* playing, step 0.., loop length in steps */
    float    ph;                        /* position in the step, 0..1 */
    uint32_t fw, slot;                  /* zm_fw_ok() and the sound source's slot, at the step start */
    uint32_t cnt[MB_NTRK], on[MB_NTRK]; /* per track: bus event counter last sent, a note is on */
    uint32_t vph[MB_NTRK], vinc[MB_NTRK], vgate[MB_NTRK];  /* own voice per track: phase, step per sample, note held */
    float    venv[MB_NTRK], vtgt[MB_NTRK], vlp[MB_NTRK];   /* own voice per track: level, its target, low-pass */
    float    vgn[MB_NTRK];              /* own voice per track: gain (SOUND's level, at the note on) */
    uint32_t lk, lfw, lslot, lwait, lok;    /* lock: calls to the next look, its fw, slot, looks waited, shadow valid */
    uint32_t sh[MB_NS], pend[MB_NS];    /* lock: shadow (storage knobs' record values), values at the last look */
#ifndef MB_SELFTEST
    uint32_t sv;                        /* storage: 1 = loaded (saves allowed) */
    uint32_t pk, pwait, snd;            /* packer: 0 idle, 1 waiting, 2 running; calls left; a SOUND to write */
    uint32_t tick, last;                /* calls to the next poll; TSCL at the previous call */
    uint32_t np, sb, sig;               /* chain as polled: parts, SndBank is part 1, layout */
    uint32_t psig, ppatch;              /* the previous poll's layout and patch number */
    uint32_t csig, cpatch;              /* as last loaded / saved */
    uint32_t full, over;                /* calls left of the FULL name; the RAM melody may not fit (no refusals) */
    uint32_t unset, settle;             /* the poll saw rows unlike their records; polls left of the settle window */
    uint32_t pslot[MP_MAXPARTS];        /* slot of part q (part 0: the own) */
    uint32_t rw[MB_NW], pw[MB_NW], cw[MB_NW];  /* storage words: this poll, the previous, last loaded / saved */
    uint32_t sov[3];                    /* TEMPO, LENGTH, GRID as last saved (= ov[0..2]) */
    uint32_t dslot, dt, dwait;          /* DrumBox: slot (MB_NOSLOT none); MB_DB_T, MB_DB_R as last written; calls waited */
    uint32_t sbslot, sbw, sbage;        /* SongBox: slot (MB_NOSLOT none), its last clock word, calls since */
    uint8_t  sgrid[MB_NTRK][MB_MAXN];   /* the grids as last saved (a refused edit goes back) */
    uint8_t  vgrid[MB_NTRK][MP_MAXN];   /* the packer's result decoded again */
    MpJob    job;                       /* the packer */
#else
    uint32_t ti, tres, trep;            /* self-test: next decode vector, result, report timer */
    uint8_t  tgrid[2][MP_MAXN];         /* self-test: decoded grids */
#if MB_SELFTEST == 2
    uint32_t pi, pgo;                   /* self-test: next pack vector, its job started */
    MpJob    job;                       /* self-test: the pack job */
#endif
#endif
    uint32_t magic1;
} MbState;

static volatile MbState *mb_g = 0;      /* for the knob text callbacks */

/* Own voices: phase step per sample of MIDI notes 0..11 (8.18 .. 15.4 Hz,
 * 2^32 = one cycle at 44.1 kHz); note n = mb_inc[n mod 12] << (n / 12). */
static const uint32_t mb_inc[12] = {
    796254u, 843601u, 893765u, 946911u, 1003217u, 1062871u,
    1126073u, 1193033u, 1263974u, 1339134u, 1418763u, 1503127u,
};

/* Note letters (index = pitch class + 1); sharps: 2, 4, 7, 9, 11. */
static const char mb_letter[13] = ".CCDDEFFGGAAB";
#define MB_SHARPS 0xA94u                /* bit i set: letter i is a sharp */

/* Texts: one pool of NUL-terminated strings (string i = mb_str(i)), <= 7
 * characters each. Knob texts (knob 1's = its value), MENU answers, then
 * the MENU questions (the names of knobs 1, 2, 3 per option, one word per
 * knob). */
#define MB_S_MODE   0u                  /* MENU, TRK1, TRK2 */
#define MB_S_SCRLL   3u
#define MB_S_OPT    4u                  /* TEMPO, LENGTH, GRID, SOUND1, SOUND2 */
#define MB_S_PEDAL  9u                  /* TEMPO 0 (the others: the BPM digits) */
#define MB_S_LEN    10u                 /* 16 .. 128 */
#define MB_S_GRID   14u                 /* 1/16, 1/8 */
#define MB_S_SND    16u                 /* PB .. Pk (SndBank's sound order) */
#define MB_S_Q      25u                 /* 3 words per option */
#define MB_S_FULL   40u                 /* FULL: an edit refused */
#define MB_VBASE(o) ((o) == MB_O_LEN ? MB_S_LEN : (o) == MB_O_GRID ? MB_S_GRID : MB_S_SND)
static const char mb_strs[] =
    "MENU \0" "TRK1\0" "TRK2\0" "SCRLL\0"    /* "MENU ": room for a 6-character option beside it (MS) */
    "TEMPO\0" "LENGTH\0" "GRID\0" "SOUND1\0" "SOUND2\0"
    "Pedal\0"
    "16\0" "32\0" "64\0" "128\0"
    "1/16\0" "1/8\0"
    "PB\0" "Tr\0" "OJ\0" "OR\0" "EP\0" "Mr\0" "Kl\0" "Sy\0" "Pk\0"
    "Song\0" "tempo\0" "is:\0"          /* TEMPO */
    "Steps\0" "in\0" "song:\0"          /* LENGTH */
    "Step\0" "grid\0" "is:\0"           /* GRID */
    "Trk1\0" "sound\0" "is:\0"          /* SOUND1 */
    "Trk2\0" "sound\0" "is:\0"          /* SOUND2 */
    "FULL\0";

/* String i of the pool. */
#pragma FUNC_ALWAYS_INLINE(mb_str)
static inline const char *mb_str(uint32_t i)
{
    const char *p = mb_strs;
#pragma UNROLL(1)
    while (i) {
        if (*p++ == 0) i--;
    }
    return p;
}

/* Send one note event (note 0 = off) of track t to the sound source found
 * at the step start: bus t + 1 = the source's knob t + 1 word. No source:
 * to the track's own voice. Out of line: called at note events only, from
 * five places (inlined, each copy cost ~140 B). */
#pragma FUNC_CANNOT_INLINE(mb_send)
#pragma CODE_SECTION(mb_send, ".text")
static void mb_send(volatile MbState *st, uint32_t t, uint32_t note)
{
    volatile uint32_t *w;
    uint32_t cnt, oct;
    if (!st->fw || st->slot == MB_NOSLOT) {
        if (note) {                     /* the track's own voice */
            cnt = st->ov[MB_O_SND + t];
            cnt -= MB_LEVELS * ((cnt * 205u) >> 12);    /* the level: value mod 20 */
            st->vgn[t] = (float)cnt * (0.1f * MB_V_GAIN);
            oct = (note * 43u) >> 9;    /* note / 12, exact below 128 */
            st->vinc[t] = mb_inc[note - 12u * oct] << oct;
            st->vgate[t] = 1u;
            st->vtgt[t] = 1.0f;
        } else
            st->vgate[t] = 0u;
        return;
    }
    /* still SndBank there? (a delete before it moves it within the step:
     * the old slot then holds the next effect: this event is dropped, the
     * own voices play until the next step start finds SndBank again) */
    if (st->slot >= zm_fws[st->fw - 1u].slots
        || ZM_U32(zm_fws[st->fw - 1u].rec + 44u * st->slot + 4u) != MB_SYN_ID) {
        st->slot = MB_NOSLOT;
        return;
    }
    w = (volatile uint32_t *)(uintptr_t)(zm_fws[st->fw - 1u].tab + 44u * st->slot + 4u * (NB_COL + t));
    cnt = st->cnt[t] >= 15u ? 1u : st->cnt[t] + 1u;
    st->cnt[t] = cnt;
    *w = (*w & NB_LOWMASK) | NB_EVENT(cnt, note);
}

#ifndef MB_SELFTEST
/* Storage code: rarely run, one copy in .text, called */
#define MB_COLD(f) MP_COLD(f)

/* The chain as it is now -> st->pslot, np, sb, sig and its words -> st->rw
 * (missing parts 0): part 0 the own knobs 4-9, then in the owner range
 * SndBank (the first one) and the Floppies in marker order; with `mark`
 * the other Floppies in range (unmarked, a repeated marker) get the next
 * markers. st->unset: a storage row (own knobs 4-9, SndBank, the Floppies
 * in range) differs from its record: after a patch change the OS fills the
 * rows from the records late, so nothing is marked and the poll doesn't
 * count. */
MB_COLD(mb_chain)
static void mb_chain(volatile MbState *st, const ZmFw *t, uint32_t me, uint32_t mark)
{
    uint32_t ns = t->slots, inr = 0u, np = 1u, sb = 0u, mf = 0u, i, s, f, q, c, p, v, sh, sig, id;
    st->pslot[0] = me;
#pragma UNROLL(1)
    for (i = 1u; i < ns; i++) {         /* owner range: up to the next storage user */
        s = me + i < ns ? me + i : me + i - ns;
        if (zm_s_user(ZM_U32(t->rec + 44u * s + 4u))) break;
        inr |= 1u << s;
    }
    st->unset = 0u;
#pragma UNROLL(1)
    for (s = 0u; s < ns; s++) {         /* rows as their records */
        id = ZM_U32(t->rec + 44u * s + 4u);
        if (s != me && !((inr >> s & 1u) && (id == MB_SYN_ID || id == ZM_FLOPPY_ID))) continue;
#pragma UNROLL(1)
        for (c = s == me ? 3u : 0u; c < 9u; c++) {
            p = 44u * s + 4u * (2u + c);
            if ((ZM_U32(t->tab + p) ^ ZM_U32(t->rec + p)) & ((1u << zm_s_wid[c]) - 1u)) st->unset = 1u;
        }
    }
    if (st->unset) mark = 0u;
#pragma UNROLL(1)
    for (i = 1u; i < ns && !sb; i++) {  /* SndBank */
        s = me + i < ns ? me + i : me + i - ns;
        if ((inr >> s & 1u) && ZM_U32(t->rec + 44u * s + 4u) == MB_SYN_ID) {
            st->pslot[1] = s;
            sb = 1u;
            np = 2u;
        }
    }
#pragma UNROLL(1)
    for (f = 1u; f <= 4u; f++)          /* Floppies with markers, in marker order */
#pragma UNROLL(1)
        for (i = 1u; i < ns; i++) {
            s = me + i < ns ? me + i : me + i - ns;
            if ((inr >> s & 1u) && ZM_U32(t->rec + 44u * s + 4u) == ZM_FLOPPY_ID
                && (ZM_U32(t->tab + 44u * s + 8u) & 15u) == MB_FL_MARK + f) {
                if (np < MP_MAXPARTS) st->pslot[np++] = s;
                inr &= ~(1u << s);
                mf = f;
                break;
            }
        }
    if (mark)
#pragma UNROLL(1)
        for (i = 1u; i < ns; i++) {     /* the other Floppies: the next markers */
            s = me + i < ns ? me + i : me + i - ns;
            if ((inr >> s & 1u) && ZM_U32(t->rec + 44u * s + 4u) == ZM_FLOPPY_ID
                && mf < 4u && np < MP_MAXPARTS) {
                mf++;
                zm_set_col(t, s, 2u, (ZM_U32(t->tab + 44u * s + 8u) & 0x1FF0u) | (MB_FL_MARK + mf));
                st->pslot[np++] = s;
            }
        }
    sig = np | sb << 4;
#pragma UNROLL(1)
    for (q = 0u; q < MP_MAXPARTS; q++) {
        st->rw[4u * q] = 0u; st->rw[4u * q + 1u] = 0u; st->rw[4u * q + 2u] = 0u; st->rw[4u * q + 3u] = 0u;
        if (q >= np) continue;
        s = st->pslot[q];
        sig |= s << (8u + 4u * q);
#pragma UNROLL(1)
        for (c = q ? 0u : 3u, p = 0u; c < 9u; c++) {    /* part 0: columns 5..10 */
            v = ZM_U32(t->tab + 44u * s + 4u * (2u + c)) & ((1u << zm_s_wid[c]) - 1u);
            sh = p & 31u;
            st->rw[4u * q + (p >> 5)] |= v << sh;
            if (sh + zm_s_wid[c] > 32u) st->rw[4u * q + (p >> 5) + 1u] |= v >> (32u - sh);
            p += zm_s_wid[c];
        }
    }
    st->np = np;
    st->sb = sb;
    st->sig = sig;
}

/* The polled chain is the saved one now: words, layout, patch number. */
MB_COLD(mb_take)
static void mb_take(volatile MbState *st, uint32_t patch)
{
    uint32_t i;
#pragma UNROLL(1)
    for (i = 0u; i < MB_NW; i++) {
        st->cw[i] = st->rw[i];
        st->pw[i] = st->rw[i];
    }
    st->csig = st->sig;
    st->psig = st->sig;
    st->cpatch = patch;
    st->ppatch = patch;
}

/* The grids and TEMPO / LENGTH / GRID as saved now (or back to the saved ones). */
MB_COLD(mb_keep)
static void mb_keep(volatile MbState *st, uint32_t back)
{
    uint32_t i, k;
#pragma UNROLL(1)
    for (k = 0u; k < MB_NTRK; k++)
#pragma UNROLL(1)
        for (i = 0u; i < MB_MAXN; i++) {
            if (back) st->grid[k][i] = st->sgrid[k][i];
            else st->sgrid[k][i] = st->grid[k][i];
        }
#pragma UNROLL(1)
    for (i = 0u; i < 3u; i++) {         /* TEMPO, LENGTH, GRID */
        if (back) st->ov[i] = st->sov[i];
        else st->sov[i] = st->ov[i];
    }
}

/* Load: the polled chain's words -> grids, TEMPO, LENGTH, GRID (and the
 * sounds from SndBank's part); the knobs jump to them. */
MB_COLD(mb_load)
static void mb_load(volatile MbState *st, uint32_t patch)
{
    uint32_t n, s8, tp, x;
    n = mp_unpack((const uint32_t *)st->rw, st->np, st->sb, st->grid[0], st->grid[1], &s8, &tp);
    st->ov[MB_O_TEMPO] = tp;
    st->ov[MB_O_LEN] = n == 16u ? 0u : n == 32u ? 1u : n == 64u ? 2u : 3u;
    st->ov[MB_O_GRID] = s8;
    if (st->sb) {
        x = st->rw[4] & 0xFFu;
        st->ov[MB_O_SND] = x > MB_OMAX(MB_O_SND) ? MB_SND_DEF : x;
        x = st->rw[4] >> 8 & 0xFFu;
        st->ov[MB_O_SND + 1u] = x > MB_OMAX(MB_O_SND) ? MB_SND_DEF : x;
    }
    if (st->off >= n) st->off = n - 1u;
    if (!st->sv || patch != st->cpatch)
        st->play = 0u;                  /* first load of a patch: playback starts again at step 1 */
    else
        st->len = n;                    /* another writer: the loop takes the new length now */
    mb_keep(st, 0u);
    mb_take(st, patch);
    st->mode = 3u;                      /* the knobs jump to the loaded values */
    st->pk = 0u;
    st->snd = 0u;
    st->full = 0u;
    st->over = 0u;
    st->settle = MB_SETTLE;
    st->sv = 1u;
}

/* Write the packer's words into the chain's parts (only changed columns;
 * Floppy markers by place, SndBank's sound values; SndBank's knob 1 / 2 table
 * words keep their bus bits; the own knobs' lock shadow follows). */
MB_COLD(mb_write)
static void mb_write(volatile MbState *st, const ZmFw *t, const volatile uint32_t *w)
{
    uint32_t q, c, p, v, sh, s, o, x, sbp;
#pragma UNROLL(1)
    for (q = 0u; q < st->np; q++) {
        s = st->pslot[q];
        sbp = st->sb && q == 1u;
#pragma UNROLL(1)
        for (c = q ? 0u : 3u, p = 0u; c < 9u; c++) {
            sh = p & 31u;
            v = w[4u * q + (p >> 5)] >> sh;
            if (sh + zm_s_wid[c] > 32u) v |= w[4u * q + (p >> 5) + 1u] << (32u - sh);
            v &= (1u << zm_s_wid[c]) - 1u;
            if (sbp && c < 2u)          /* the sound values: knob 1's field, knob 2's bits 0..2 */
                v = c ? (v & ~7u) | MB_SV(st) >> NB_LOW : MB_SV(st) & NB_LOWMASK;
            else if (q && c == 0u)
                v = (v & ~15u) | (MB_FL_MARK + q - st->sb);
            o = 44u * s + 4u * (2u + c);
            x = sbp && c < 2u ? (ZM_U32(t->tab + o) & ~NB_LOWMASK) | v : v;
            if (ZM_U32(t->tab + o) != x) *(volatile uint32_t *)(uintptr_t)(t->tab + o) = x;
            if (ZM_U32(t->rec + o) != v) *(volatile uint32_t *)(uintptr_t)(t->rec + o) = v;
            if (!q) {
                st->sh[c - 3u] = v;
                st->pend[c - 3u] = v;
            }
            p += zm_s_wid[c];
        }
    }
}

/* SOUND1 / SOUND2 into SndBank's part (bits 0..15: its knob 1 field, then
 * bits 0..2 of its knob 2 field). */
MB_COLD(mb_sounds)
static void mb_sounds(volatile MbState *st, const ZmFw *t)
{
    uint32_t o = 44u * st->pslot[1] + 4u * NB_COL, v = MB_SV(st), c, m, x;
#pragma UNROLL(1)
    for (c = 0u; c < 2u; c++, o += 4u) {
        m = c ? 7u : NB_LOWMASK;
        x = c ? v >> NB_LOW : v & NB_LOWMASK;
        *(volatile uint32_t *)(uintptr_t)(t->tab + o) = (ZM_U32(t->tab + o) & ~m) | x;
        *(volatile uint32_t *)(uintptr_t)(t->rec + o) = (ZM_U32(t->rec + o) & ~m) | x;
    }
    st->cw[4] = (st->cw[4] & ~0xFFFFu) | v;
}

/* The packer finished: save if it fits the chain and decodes back to the
 * grids, else refuse the edits since the last save (over: keep them). */
MB_COLD(mb_done)
static void mb_done(volatile MbState *st, const ZmFw *t, uint32_t me)
{
    uint32_t n = 16u << st->ov[MB_O_LEN], ok, s8, tp, i, k, v, prev;
    ok = !st->job.err && st->job.parts && st->job.parts <= st->np;
    if (ok) {
        ok = mp_unpack((const uint32_t *)st->job.words, st->np, st->sb, st->vgrid[0], st->vgrid[1], &s8, &tp) == n
             && s8 == st->ov[MB_O_GRID] && tp == st->ov[MB_O_TEMPO];
#pragma UNROLL(1)
        for (k = 0u; k < MB_NTRK && ok; k++)
#pragma UNROLL(1)
            for (i = 0u, prev = MB_REST; i < n && ok; i++) {    /* as mp_start's normalising */
                v = st->grid[k][i];
                if (v > MB_MAXVAL || (v == MB_HOLD && prev == MB_REST)) v = MB_REST;
                ok = st->vgrid[k][i] == v;
                prev = v;
            }
    }
    if (ok) {
        mb_write(st, t, st->job.words);
        mb_chain(st, t, me, 0u);
        mb_take(st, ZM_U32(t->patch));
        mb_keep(st, 0u);
        st->over = 0u;
    } else {
        if (!st->over) {
            mb_keep(st, 1u);
            st->mode = 3u;              /* the knobs jump back */
        }
        st->full = MB_FULL;
    }
    st->pk = 0u;
}

/* Storage, every call (known firmware, own slot in range): polls, loads,
 * the packer, saves. */
MB_COLD(mb_store)
static void mb_store(volatile MbState *st, const ZmFw *t, uint32_t me)
{
    uint32_t t0 = TSCL, i, x, same, patch;
    if (t0 - st->last > MB_AWAY_CYC) st->sv = 0u;   /* away (ctx[3] may have been lent out): load again */
    st->last = t0;
    if (++st->tick >= MB_POLL) {
        st->tick = 0u;
        patch = ZM_U32(t->patch);
        mb_chain(st, t, me, st->sv && patch == st->cpatch && !st->settle);  /* markers: loaded, settled */
        same = st->sig == st->psig && patch == st->ppatch && !st->unset;
        if (st->settle) st->settle--;
        x = 0u;
#pragma UNROLL(1)
        for (i = 0u; i < MB_NW; i++) {
            if (st->rw[i] != st->pw[i]) same = 0u;
            st->pw[i] = st->rw[i];
            x |= st->rw[i] ^ st->cw[i];
        }
        st->psig = st->sig;
        st->ppatch = patch;
        if (same) {                     /* two polls agree */
            if (!st->sv || patch != st->cpatch) mb_load(st, patch);
            else if (st->sig != st->csig && (st->settle || ((st->rw[0] ^ st->cw[0]) | (st->rw[1] ^ st->cw[1])
                                                            | (st->rw[2] ^ st->cw[2])) != 0u))
                mb_load(st, patch);     /* a part late after a load, or a whole patch sent (own part changed) */
            else if (st->sig != st->csig) {     /* a storage effect added / removed: the RAM melody into it */
                mb_take(st, patch);
                st->pk = 1u;
                st->pwait = 0u;
                st->over = 1u;
            } else if (x) mb_load(st, patch);   /* another writer */
        }
    }
    if (!st->sv) return;
    if (st->snd) {
        st->snd = 0u;
        if (st->sb) mb_sounds(st, t);
    }
    if (st->pk == 1u) {
        if (st->pwait) st->pwait--;
        else {
            mp_start((MpJob *)&st->job, st->grid[0], st->grid[1], 16u << st->ov[MB_O_LEN],
                     st->ov[MB_O_GRID], st->ov[MB_O_TEMPO], st->sb);
            st->pk = 2u;
        }
    } else if (st->pk == 2u) {
#pragma UNROLL(1)
        for (i = 0u; i < MB_PK_MAX; i++) {
            if (mp_run((MpJob *)&st->job, 1u)) {
                mb_done(st, t, me);
                break;
            }
            if (TSCL - t0 >= MB_PK_CYC) break;
        }
    }
}

/* DrumBox in the chain (the first one; none while SongBox leads, i.e. its
 * clock word carries MB_SB_SYNC: it drives DrumBox then, and DrumBox's
 * Tempo word gets its record's value back once) plays its own song (its
 * knobs as set) on our clock:
 * DrumBox's synced mode (DB_SYNC in drumbox.c). Its Tempo word in the table
 * row := TEMPO | MB_DB_SYNC, MB_DB_PLAY while we play (loaded: before the
 * load the loop starts again), the quarter of our bar reached, MB_DB_T
 * flipped with flip (at each quarter; at each poll while stopped as a
 * heartbeat) and MB_DB_R (flipped at each start). DrumBox puts its 16ths on
 * each flipped quarter's start, starts its song again at a flip of MB_DB_R
 * and starts at a flip to quarter 0. Loaded: its record's Tempo := TEMPO (both
 * 0 = Pedal, else BPM = 39 + value; its page shows it, a save keeps it);
 * before that the record's value. */
MB_COLD(mb_drums)
static void mb_drums(volatile MbState *st, const ZmFw *t, uint32_t me, uint32_t flip)
{
    uint32_t s, id, ds = MB_NOSLOT, w, sb = MB_NOSLOT;
    volatile uint32_t *tb, *rc;
#pragma UNROLL(1)
    for (s = 0u; s < t->slots; s++) {
        id = ZM_U32(t->rec + 44u * s + 4u);
        if (id == MB_SB_ID && sb == MB_NOSLOT) sb = s;      /* its clock: we follow it while it leads */
        if (id == MB_DB_ID && ds == MB_NOSLOT && s != me) ds = s;
    }
    st->sbslot = sb;
    if (sb != MB_NOSLOT && st->sbage < MB_SB_HOLD) {
        if (st->dslot < t->slots && ZM_U32(t->rec + 44u * st->dslot + 4u) == MB_DB_ID)
            *(volatile uint32_t *)(uintptr_t)(t->tab + 44u * st->dslot + 4u * MB_DB_TEMPO)
                = ZM_U32(t->rec + 44u * st->dslot + 4u * MB_DB_TEMPO);
        ds = MB_NOSLOT;
    }
    st->dslot = ds;
    if (ds == MB_NOSLOT) return;
    tb = (volatile uint32_t *)(uintptr_t)(t->tab + 44u * ds + 4u * MB_DB_TEMPO);
    rc = (volatile uint32_t *)(uintptr_t)(t->rec + 44u * ds + 4u * MB_DB_TEMPO);
    w = *rc & 0xFFu;
    if (st->sv) {
        w = st->ov[MB_O_TEMPO];
        if (*rc != w) *rc = w;
    }
    if (flip) st->dt ^= MB_DB_T;
    w |= MB_DB_SYNC | st->dt | (*tb & MB_DB_READY);    /* DrumBox's bit kept */
    if (st->play && st->sv)             /* the quarter: 16ths into the bar / 4 */
        w |= MB_DB_PLAY | ((st->ov[MB_O_GRID] ? st->step << 1 : st->step) >> 2 & 3u) << MB_DB_QSH;
    if (*tb != w) *tb = w;
}
#define MB_EDITED(st) ((st)->pk = 1u, (st)->pwait = MB_PK_WAIT)
#define MB_DRUMS(go) do { if (dz) mb_drums(st, dz, (uint32_t)ctx[0], (go)); } while (0)
/* a start waits (loaded) while DrumBox's kit isn't ready, up to MB_DB_WAIT calls */
#define MB_DWAIT (dz && st->sv && st->dslot < dz->slots && ++st->dwait < MB_DB_WAIT \
                  && !(ZM_U32(dz->tab + 44u * st->dslot + 4u * MB_DB_TEMPO) & MB_DB_READY))
#else
#define MB_EDITED(st) ((void)0)
#define MB_DRUMS(go) ((void)0)
#define MB_DWAIT 0
#endif

void Fx_SFX_MeloBox(unsigned int *ctx)
{
    unsigned int *magicSrc = (unsigned int *)(uintptr_t)ctx[12];
    unsigned int *magicDst = (unsigned int *)(uintptr_t)(*(unsigned int *)(uintptr_t)ctx[11]);
    float *params = (float *)(uintptr_t)ctx[1];
    volatile unsigned int *arena = (volatile unsigned int *)(uintptr_t)ctx[3];
    volatile MbState *st;
    uint32_t mode, r2, r3, k, n, x, tens, i, g, changed = 0u, fw, own, nmax, t;
    uint32_t sbw = 0u;                  /* SongBox's clock word while we follow it (0: our own clock) */
    volatile uint8_t *grid;
#ifndef MB_SELFTEST
    const ZmFw *dz = 0;                 /* rows for DrumBox (own slot, known firmware) */
#endif

    *magicDst = *magicSrc;              /* audio passes through untouched */

    if (!arena) return;
    {
        uintptr_t base = (uintptr_t)arena[0];
        uintptr_t end = (uintptr_t)arena[1] & ~(uintptr_t)3u;
        if (base == 0u || end <= base + sizeof(MbState)) return;
        st = (volatile MbState *)(end - sizeof(MbState));
    }
    mode = zm_knob(params[5 + MB_MODE], 2u);         /* 0 MENU, 1 TRK1, 2 TRK2 */
    r2 = zm_knob(params[5 + MB_EDIT], MB_MAXVAL);
    r3 = zm_knob(params[5 + MB_SCRLL], MB_MAXK3);

    if (st->magic0 != MB_MAGIC || st->magic1 != MB_MAGIC) {
        /* Clear all of it first: after a slot move or a patch switch the
         * arena holds another effect's data, and a field missed below would
         * keep it (DrumBox froze the pedal that way). Volatile word stores:
         * no unrolled uninterruptible store block. Every field not set
         * below starts at 0. */
        volatile uint32_t *w = (volatile uint32_t *)st;
#pragma UNROLL(1)
        for (i = 0u; i < sizeof(MbState) / 4u; i++)
            w[i] = 0u;
        st->magic0 = MB_MAGIC;
#pragma UNROLL(1)
        for (i = 0u; i < MB_MAXN; i++) {
            st->grid[0][i] = MB_REST;
            st->grid[1][i] = MB_REST;
        }
        /* MENU: the option knob 2 shows in the patch (the load's jump keeps it);
         * options at 0: TEMPO Pedal, LENGTH 16 steps, GRID 1/16 */
        st->opt = mode ? MB_O_TEMPO : r2 >= MB_NOPT ? MB_NOPT - 1u : r2;
        st->ov[MB_O_SND] = MB_SND_DEF;  /* PB at 50, both tracks */
        st->ov[MB_O_SND + 1u] = MB_SND_DEF;
        st->off = r3 > 15u ? 15u : r3;
        if (mode) st->grid[mode - 1u][st->off] = (uint8_t)r2;   /* knob 2's patch value: its step's */
        st->mode = mode ? mode : 3u;    /* MENU: the knobs jump to its values below */
        st->edit = r2;
        st->k3 = r3;
        st->len = 16u;
        st->slot = MB_NOSLOT;
        st->lslot = (uint32_t)ctx[0];
#ifndef MB_SELFTEST
        st->psig = 0xFFFFFFFFu;
        st->ppatch = 0xFFFFFFFFu;
        st->dslot = MB_NOSLOT;
        st->sbslot = MB_NOSLOT;
        st->sbage = MB_SB_HOLD;
#endif
        st->magic1 = MB_MAGIC;
    }
    mb_g = st;                          /* the active copy (knob texts) */
    n = 16u << st->ov[MB_O_LEN];
    fw = zm_fw_ok();
    own = fw && (uint32_t)ctx[0] < zm_fws[fw - 1u].slots       /* and the own slot's record is still ours */
          && ZM_U32(zm_fws[fw - 1u].rec + 44u * (uint32_t)ctx[0] + 4u) == ZM_S_ID(MB_FXID);  /* (else being replaced: the rows aren't ours) */

    /* lock of the storage knobs: a look every MB_LOOK calls */
    if (st->lk) st->lk--;
    else {
        st->lk = MB_LOOK;
        st->lfw = fw;
        if (!own || (uint32_t)ctx[0] != st->lslot) {
            st->lok = 0u;               /* no rows, or moved: wait again */
            st->lwait = 0u;
            st->lslot = (uint32_t)ctx[0];
        } else {
            volatile uint32_t *rr = (volatile uint32_t *)(uintptr_t)
                (zm_fws[fw - 1u].rec + 44u * (uint32_t)ctx[0] + 4u * MB_SCOL);
            if (!st->lok) {
                if (++st->lwait >= MB_LWAIT) {
#pragma UNROLL(1)
                    for (i = 0u; i < MB_NS; ++i) {
                        x = rr[i];
                        st->sh[i] = x;
                        st->pend[i] = x;
                    }
                    st->lok = 1u;
                }
            } else {
#pragma UNROLL(1)
                for (i = 0u; i < MB_NS; ++i) {
                    x = rr[i];
                    if (x != st->sh[i] && x == st->pend[i])
                        st->sh[i] = x;  /* the same twice: another writer's */
                    st->pend[i] = x;
                }
            }
        }
    }

#ifndef MB_SELFTEST
    /* storage: load, save (a load or a refusal sets the grids, LENGTH and
     * mode 3: the knobs jump below) */
    if (own) mb_store(st, &zm_fws[fw - 1u], (uint32_t)ctx[0]);
    if (st->full) st->full--;
    /* DrumBox: stopped every call until loaded (after a patch change the OS
     * fills the rows from the records a few calls late), while stopped at
     * each poll (a heartbeat); being replaced: DrumBox's Tempo word back to
     * its record's (no sync bits: on its own at once) */
    if (own) {
        dz = &zm_fws[fw - 1u];
        if (!st->sv || (!st->play && st->tick == 0u)) MB_DRUMS(st->sv);
    } else if (fw && st->dslot < zm_fws[fw - 1u].slots) {
        x = 44u * st->dslot;
        if (ZM_U32(zm_fws[fw - 1u].rec + x + 4u) == MB_DB_ID)
            *(volatile uint32_t *)(uintptr_t)(zm_fws[fw - 1u].tab + x + 4u * MB_DB_TEMPO)
                = ZM_U32(zm_fws[fw - 1u].rec + x + 4u * MB_DB_TEMPO);
        st->dslot = MB_NOSLOT;
    }
    /* Being replaced or removed (known OS, our slot's record is another
     * effect's or none): nothing more outside the own state, no knob names /
     * maxes in the descriptor, no redraw posts, no note words (removing
     * MelodyBox while it was on froze the pedal; the audio passes through). */
    if (fw && !own) return;
    /* SongBox in the chain (slot from mb_drums): its clock word, every call;
     * the last one with MB_SB_SYNC stays MB_SB_HOLD calls (a turn of its
     * Tempo knob writes the plain value for a call) */
    if (own && st->sbslot < zm_fws[fw - 1u].slots
        && ZM_U32(zm_fws[fw - 1u].rec + 44u * st->sbslot + 4u) == MB_SB_ID) {
        x = ZM_U32(zm_fws[fw - 1u].tab + 44u * st->sbslot + 4u * MB_SB_COL);
        if (x & MB_SB_SYNC) {
            st->sbw = x;
            st->sbage = 0u;
        } else if (st->sbage < MB_SB_HOLD) st->sbage++;
        if (st->sbage < MB_SB_HOLD) sbw = st->sbw;
    }
    n = 16u << st->ov[MB_O_LEN];
#endif

    /* knob k jumps to v (own patch columns + params, as zmenu's jump);
     * unknown firmware: it stays (pickup). Not before the first load: the
     * knobs keep the patch's values (a jump to the defaults would reach the
     * patch record while the OS is still loading it, and its saved copy:
     * the patch shows EDITED); the load's jump (mode 3) sets the same values */
#ifndef MB_SELFTEST
#define MB_CANJUMP (own && st->sv)
#else
#define MB_CANJUMP own
#endif
#define MB_JUMP(k, v, r) do { \
        if (MB_CANJUMP) { \
            zm_set_col(&zm_fws[fw - 1u], (uint32_t)ctx[0], 2u + (k), (v)); \
            params[5 + (k)] = (float)(v) * 0.01f; \
            (r) = (v); \
        } \
    } while (0)

#ifndef MB_SELFTEST
    if (!st->sv || st->settle) {
        /* not loaded or settling: knobs 2 and 3 moving is the OS filling
         * the patch in, not an edit (the scroll offset follows knob 3) */
        st->edit = r2;
        st->k3 = r3;
        if (mode) st->off = r3 >= n ? n - 1u : r3;
    }
#endif

    if (mode != st->mode) {
        /* another mode: knobs 2 and 3 jump to its values (a track: the
         * shown step's value, the scroll offset) */
        uint32_t v2, v3;
        st->mode = mode;
        if (!mode) {
            v2 = st->opt;
            v3 = st->ov[st->opt];
        } else {
            v2 = st->grid[mode - 1u][st->off];
            v3 = st->off;
        }
        MB_JUMP(MB_EDIT, v2, r2);
        MB_JUMP(MB_SCRLL, v3, r3);
        st->edit = r2;
        st->k3 = r3;
    } else if (!mode) {
        if (r2 != st->edit) {
            /* MENU: another option; knob 3 jumps to its value */
            st->edit = r2;
            st->opt = r2 >= MB_NOPT ? MB_NOPT - 1u : r2;
            x = st->ov[st->opt];
            MB_JUMP(MB_SCRLL, x, r3);
            st->k3 = r3;
        } else if (r3 != st->k3) {
            /* MENU: the option's new value */
            st->k3 = r3;
            x = MB_OMAX(st->opt);
            st->ov[st->opt] = r3 > x ? x : r3;
            n = 16u << st->ov[MB_O_LEN];
            if (st->off >= n) st->off = n - 1u;
#ifndef MB_SELFTEST
            if (st->opt >= MB_O_SND) st->snd = 1u;  /* written straight into SndBank's part */
            else MB_EDITED(st);
#endif
        }
    } else {
        grid = st->grid[mode - 1u];
        x = r3 >= n ? n - 1u : r3;
        if (x != st->off) {
            /* scrolled: knob 2 jumps to its new step's value */
            st->off = x;
            g = grid[x];
            MB_JUMP(MB_EDIT, g, r2);
            st->edit = r2;
        } else if (r2 != st->edit) {
            /* knob 2 turned: the value of its step */
            st->edit = r2;
            grid[x] = (uint8_t)r2;
            MB_EDITED(st);
        }
        st->k3 = r3;
    }
#undef MB_JUMP
#undef MB_CANJUMP

    /* knob maxes (descriptor +0x0C): knob 2 the values / options, knob 3 the
     * scroll range / the option's values; written only when changed */
    {
        volatile unsigned int *d = (volatile unsigned int *)ZM_DESC;
        uint32_t m2 = !st->mode ? MB_NOPT - 1u : MB_MAXVAL;
        uint32_t m3 = !st->mode ? MB_OMAX(st->opt) : n - 1u;
        if (d[12u * (2u + MB_EDIT) + 3u] != m2) d[12u * (2u + MB_EDIT) + 3u] = m2;
        if (d[12u * (2u + MB_SCRLL) + 3u] != m3) d[12u * (2u + MB_SCRLL) + 3u] = m3;
    }

    /* knob k's name. MENU: the option's question word k. A track: item off
     * + k: "" for item 0, "END" after step n, else the step's 2 digits, then
     * "___" (rest), "Hld" (hold), "_" + a natural note + octave or a sharp
     * note + octave */
    nmax = n;
    grid = st->grid[st->mode ? st->mode - 1u : 0u];
#pragma UNROLL(1)
    for (k = 0u; k < MB_NKNOB; k++) {
        volatile unsigned char *nm = (volatile unsigned char *)&ZM_DESC[12u * (2u + k)];
        const char *q = 0;
        uint32_t item = st->off + k, c2, c3, c4, o, end = 0u;        if (!st->mode) q = mb_str(MB_S_Q + 3u * st->opt + k);
#ifndef MB_SELFTEST
        if (st->full && k == MB_EDIT) q = mb_str(MB_S_FULL);
#endif
        x = item >= 100u ? item - 100u : item;
        tens = (x * 205u) >> 11;        /* x / 10, exact for x < 1029 */
        g = item - 1u < nmax ? grid[item - 1u] : MB_REST;
        if (g > MB_MAXVAL) g = MB_REST;
        if (g == MB_REST) c2 = c3 = c4 = '_';
        else if (g == MB_HOLD) { c2 = 'H'; c3 = 'l'; c4 = 'd'; }
        else {
            g -= 2u;                    /* pitch 0..36 */
            o = (g * 43u) >> 9;         /* g / 12, exact for g < 128 */
            g = g - 12u * o + 1u;       /* letter index 1..12 */
            c4 = '2' + o;
            if (MB_SHARPS >> g & 1u) { c2 = (uint32_t)mb_letter[g]; c3 = '#'; }
            else { c2 = '_'; c3 = (uint32_t)mb_letter[g]; }
        }
#pragma UNROLL(1)
        for (i = 0u; i < 8u; i++) {
            unsigned char v;
            if (q) {
                v = end ? 0u : (unsigned char)q[i];
                if (v == 0u) end = 1u;
            } else {
                v = item == 0u ? 0u :
                    item > nmax ? (i == 0u ? 'E' : i == 1u ? 'N' : i == 2u ? 'D' : 0u) :
                    i == 0u ? (unsigned char)('0' + tens) :
                    i == 1u ? (unsigned char)('0' + x - 10u * tens) :
                    i == 2u ? (unsigned char)c2 :
                    i == 3u ? (unsigned char)c3 :
                    i == 4u ? (unsigned char)c4 : 0u;
            }
            if (nm[i] != v) {
                nm[i] = v;
                changed = 1u;
            }
        }
    }
    if (changed) {
        st->posts = ZM_POSTS;
        st->postWait = 0u;
    }

#ifdef MB_SELFTEST
    /* self-test: decoder, one vector per call (n, step flag, FNV-1a hash of
     * both grids + the tempo code); then (pack build, MB_SELFTEST 2) packer,
     * one P vector at a time (key, bits, parts, words); result into the own
     * knob 1 record column for `patch`: 0 running, 1 all passed, 2000 + i
     * decode vector i failed first, 3000 + i pack vector i, 4000 + i its
     * emitted bits != the DP's */
    if (st->ti < MB_NTEST) {
        uint32_t vv = mb_test.vec[st->ti][0], s8, tp, nn, h, j;
        nn = mp_unpack(&mb_test.words[vv & 0xFFFFu], vv >> 16 & 15u, vv >> 28 & 1u,
                       st->tgrid[0], st->tgrid[1], &s8, &tp);
        h = 2166136261u;
#pragma UNROLL(1)
        for (j = 0u; j < 2u * nn; j++) h = (h ^ st->tgrid[j >= nn][j >= nn ? j - nn : j]) * 16777619u;
        h = (h ^ tp) * 16777619u;
        if ((nn != 16u << (vv >> 24 & 3u) || s8 != (vv >> 20 & 15u) || h != mb_test.vec[st->ti][1])
            && !st->tres)
            st->tres = 2000u + st->ti;
        st->ti++;
#if MB_SELFTEST == 2
    } else if (st->pi < MB_NPTEST) {
        uint32_t pv = mb_test.pvec[st->pi][0], pb = mb_test.pvec[st->pi][1], parts, j, bad, nn;
        if (!st->pgo) {
            nn = 16u << (pv >> 20 & 3u);
            mp_start((MpJob *)&st->job, &mb_test.grids[pv & 0xFFFu],
                     (pv >> 23 & 1u) ? &mb_test.grids[(pv & 0xFFFu) + nn] : 0, nn, pv >> 22 & 1u,
                     pv >> 12 & 0xFFu, pb >> 15 & 1u);
            st->pgo = 1u;
        } else if (mp_run((MpJob *)&st->job, MB_TUNITS)) {
            parts = pv >> 24 & 15u;
            bad = st->job.okey != (pv >> 28 & 15u)
                  || st->job.bits != (pb & 0x7FFFu) || st->job.parts != parts;
#pragma UNROLL(1)
            for (j = 0u; j < 4u * parts && !bad; j++)
                bad = st->job.words[j] != mb_test.words[(pb >> 16) + j];
            if (!st->tres && st->job.err) st->tres = 4000u + st->pi;
            if (!st->tres && bad) st->tres = 3000u + st->pi;
            st->pgo = 0u;
            st->pi++;
        }
#endif
    } else if ((++st->trep & 4095u) == 0u) {
        if (!st->tres) st->tres = 1u;   /* all done, none failed */
        if (own)
            *(volatile uint32_t *)(uintptr_t)(zm_fws[fw - 1u].rec + 44u * (uint32_t)ctx[0] + 4u * NB_COL)
                = st->tres;
    }
#endif

    /* playback: footswitch on plays both tracks' steps over the note bus.
     * Following SongBox (sbw): it plays while SongBox plays, our step = its
     * 16ths (GRID 1/8: 8ths) from the song's bar 1, wrapped at our length
     * (so its tempo, swing and song wrap are ours; added mid-song we start
     * at once in place); the footswitch only mutes. */
    {
        uint32_t start = 0u, stop = 0u, s, on = params[0] >= 0.5f;
        int b;
        if (sbw) {
            if (!(sbw & MB_SB_PLAY)) stop = st->play;
            else {
                s = (sbw >> MB_SB_SSH & 15u) | (sbw >> MB_SB_BSH & 127u) << 4;
                if (st->ov[MB_O_GRID]) s >>= 1;
                s &= n - 1u;            /* n = 16 << LENGTH */
                if (!st->play || s != st->step) {
                    st->play = 1u;
                    st->step = s;
                    st->ph = 0.0f;
                    st->len = n;
                    start = 1u;
                }
            }
        } else if (!on) {
            stop = st->play;
        } else if (!st->play && MB_DWAIT) {     /* DrumBox's kit renders (after a patch change): both start once it's ready */
        } else if (!st->play) {         /* start at step 1 */
            st->play = 1u;
#ifndef MB_SELFTEST
            st->dt ^= MB_DB_R;          /* DrumBox: its song from the start (also without a stop) */
            st->dwait = 0u;
#endif
            st->step = 0u;
            st->ph = 0.0f;
            st->len = n;
            start = 1u;
        }
        if (st->play && !start && !stop) {
            /* the step's position (note offs at MB_GATE); TEMPO (following:
             * SongBox's Tempo value, the same code), Pedal: the pedal tempo
             * (as SongBox's ss_pedal_bpm) */
            x = st->fw;
            t = sbw ? (sbw & 0xFFu) : st->ov[MB_O_TEMPO];
            b = t ? 39 + (int)t :
                x ? (int)ZM_U32(zm_fws[x - 1u].mstride ? zm_fws[x - 1u].cursor + 8u : MB_G1_BPM) : 120;
            st->ph += (float)(b >= 40 && b <= 250 ? b : 120)
                      * (st->ov[MB_O_GRID] ? 0.5f * MB_PH_PER_BPM : MB_PH_PER_BPM);
            if (!sbw && st->ph >= 1.0f) {   /* own clock: the next step */
                st->ph -= 1.0f;
                if (++st->step >= st->len) {
                    st->step = 0u;
                    st->len = n;
                }
                start = 1u;
            }
        }
        if (stop) {                     /* footswitch off (SongBox stopped): stop */
            st->play = 0u;
            MB_DRUMS(0u);               /* DrumBox stops too (as both Len on Skip) */
            if (st->mode) {             /* knob 3: SCRLL again */
                st->posts = ZM_POSTS;
                st->postWait = 0u;
            }
#pragma UNROLL(1)
            for (t = 0u; t < MB_NTRK; t++)
                if (st->on[t]) {
                    mb_send(st, t, 0u);
                    st->on[t] = 0u;
                }
        }
        if (st->play && start) {        /* step start: the sound source */
            st->fw = x = zm_fw_ok();
            st->slot = MB_NOSLOT;
            if (x && own) {
#pragma UNROLL(1)
                for (s = 0u; s < zm_fws[x - 1u].slots; s++)
                    if (s != (uint32_t)ctx[0] && ZM_U32(zm_fws[x - 1u].rec + 44u * s + 4u) == MB_SYN_ID) {
                        st->slot = s;
                        break;
                    }
            }
            if (st->slot != MB_NOSLOT)  /* SndBank plays: the own voices stop */
                st->vgate[0] = st->vgate[1] = 0u;
            /* DrumBox: our position, a flip at each quarter (4 16ths, 2 8ths) */
            MB_DRUMS(!((st->ov[MB_O_GRID] ? st->step << 1 : st->step) & 3u));
            if (!(st->step & 11u) && st->mode) {    /* step 0 or 4 of 16: knob 3's text (position / SCRLL) */
                st->posts = ZM_POSTS;
                st->postWait = 0u;
            }
        }
#pragma UNROLL(1)
        for (t = 0u; t < MB_NTRK && st->play; t++) {
            grid = st->grid[t];
            g = grid[st->step];
            if (!on) {                  /* following, footswitch off: muted */
                if (st->on[t]) {
                    mb_send(st, t, 0u);
                    st->on[t] = 0u;
                }
            } else if (start) {         /* step start: the note */
                if (g >= 2u && g <= MB_MAXVAL) {
                    mb_send(st, t, MB_BASE + g - 2u);
                    st->on[t] = 1u;
                } else if (st->on[t] && (g != MB_HOLD || st->step == 0u)) {
                    mb_send(st, t, 0u); /* a rest (a hold at step 1 is one too) */
                    st->on[t] = 0u;
                }
            } else if (st->on[t] && st->ph >= MB_GATE) {
                s = st->step + 1u;      /* the note's last step: off, else a hold follows */
                if (s >= st->len || grid[s] != MB_HOLD) {
                    mb_send(st, t, 0u);
                    st->on[t] = 0u;
                }
            }
        }
    }

    /* the own voices: level steps once per call, ramps linearly over the 8
     * samples; added to both channels while the footswitch is on */
#pragma UNROLL(1)
    for (t = 0u; t < MB_NTRK; t++) {
        float *fx = (float *)(uintptr_t)ctx[5];
        float env0 = st->venv[t], env, de, lp, saw, gn = st->vgn[t];
        uint32_t ph, inc;
        if (st->vgate[t]) {
            st->vtgt[t] *= MB_V_FALL;
            env = env0 + (st->vtgt[t] - env0) * MB_V_ATT;
        } else
            env = env0 * MB_V_REL;
        st->venv[t] = env;
        if (params[0] >= 0.5f && (env0 > 1e-5f || env > 1e-5f)) {
            de = (env - env0) * 0.125f;
            ph = st->vph[t];
            inc = st->vinc[t];
            lp = st->vlp[t];
            for (i = 0u; i < 8u; ++i) {
                ph += inc;
                saw = (float)(int)ph * (1.0f / 2147483648.0f);
                lp += (saw - lp) * MB_V_LP;
                env0 += de;
                saw = lp * env0 * gn;
                fx[i] += saw;
                fx[8u + i] += saw;
            }
            st->vph[t] = ph;
            st->vlp[t] = lp;
        }
    }

    /* redraw after a rename (only on a known OS, own card on screen) */
    if (st->posts) {
        if (st->postWait) st->postWait--;
        else {
            st->posts--;
            st->postWait = ZM_POST_GAP;
            if (fw) {
                const ZmFw *t = &zm_fws[fw - 1u];
                if (zm_visible(t, (uint32_t)ctx[0]) && zm_in_ram(t->event) && !(t->event & 3u)) {
                    uint32_t ev = ZM_U32(t->event);
                    if (zm_in_ram(ev) && !(ev & 3u))
                        ((zm_post_fn)t->post)((void *)(uintptr_t)ev, 0x01u);
                }
            }
        }
    }
}

/* ---- knob texts (UI thread) ---- */

/* Knob k (0-based), value v: knob 1 MENU / TRK1 / TRK2; knob 2 its own
 * name (a track: descriptor entry 3 as the audio callback wrote it; on
 * steps 1, 5, 9, .. glyph 0x1D + "_" over the step number) or the
 * MENU option; knob 3 SCRLL (playing: the first 4 steps of each 16 the
 * 16's first step, "St:01", "St:17", .., "S:113") or the MENU option's
 * value (TEMPO: Pedal or the BPM). One shared copy in .text. */
#pragma CODE_SECTION(mb_text, ".text")
#pragma FUNC_CANNOT_INLINE(mb_text)
static int mb_text(uint32_t k, uint32_t v, char *s)
{
    const volatile MbState *st = mb_g;
    const volatile unsigned char *nm = (const volatile unsigned char *)&ZM_DESC[12u * 3u];
    const char *p;
    uint32_t menu = st && st->mode == 0u, o, lv = MB_LEVELS;
    int n = 0;
    if (k == MB_MODE) p = mb_str(MB_S_MODE + (v < 2u ? v : 2u));
    else if (k == MB_EDIT && menu) p = mb_str(MB_S_OPT + (v < MB_NOPT ? v : MB_NOPT - 1u));
    else if (k == MB_EDIT) {
#pragma UNROLL(1)
        while (n < 7 && nm[n] >= ' ' && nm[n] <= '~') {
            s[n] = (char)nm[n];
            n++;
        }
        if (n > 1 && nm[0] <= '9' && st && !(st->off & 3u)) {  /* step 1, 5, 9, ..: a mark for the number */
            s[0] = 0x1D;
            s[1] = '_';
        }
        s[n] = 0;
        return n;
    } else if (menu) {
        o = st->opt < MB_NOPT ? st->opt : MB_NOPT - 1u;
        if (v > MB_OMAX(o)) v = MB_OMAX(o);
        if (o == MB_O_TEMPO && v) {     /* the BPM, 40..250 */
            v += 39u;
            if (v >= 100u) {
                o = v >= 200u ? 2u : 1u;
                s[n++] = (char)('0' + o);
                v -= 100u * o;
            }
            o = (v * 205u) >> 11;       /* v / 10, exact for v < 1029 */
            s[n++] = (char)('0' + o);
            s[n++] = (char)('0' + v - 10u * o);
            s[n] = 0;
            return n;
        }
        if (o >= MB_O_SND) {            /* sound value: sound, level */
            lv = v;
            v = (v * 205u) >> 12;       /* v / 20, exact below 1024 */
            lv -= MB_LEVELS * v;
        }
        p = o == MB_O_TEMPO ? mb_str(MB_S_PEDAL) : mb_str(MB_VBASE(o) + v);
    } else if (st && st->play && !(st->step & 12u)) {
        v = (st->step & ~15u) + 1u;     /* playing, the first 4 steps of each 16: the position, 1 .. 113 */
        s[n++] = 'S';
        if (v >= 100u) {                /* "S:101" */
            s[n++] = ':';
            s[n++] = '1';
            v -= 100u;
        } else {                        /* "St:01" */
            s[n++] = 't';
            s[n++] = ':';
        }
        o = (v * 205u) >> 11;           /* v / 10, exact for v < 1029 */
        s[n++] = (char)('0' + o);
        s[n++] = (char)('0' + v - 10u * o);
        s[n] = 0;
        return n;
    } else p = mb_str(MB_S_SCRLL);
#pragma UNROLL(1)
    for (; n < 7 && p[n]; n++) s[n] = p[n];
    if (lv < MB_LEVELS) {               /* SOUND: ":" + the level x 5, two digits ("EP:70") */
        s[n++] = ':';
        s[n++] = (char)('0' + (lv >> 1));
        s[n++] = (lv & 1u) ? '5' : '0';
    }
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_Mb1, ".text")
int GetString_Mb1(unsigned int v, char *s) { return mb_text(0u, v, s); }
#pragma CODE_SECTION(GetString_Mb2, ".text")
int GetString_Mb2(unsigned int v, char *s) { return mb_text(1u, v, s); }
#pragma CODE_SECTION(GetString_Mb3, ".text")
int GetString_Mb3(unsigned int v, char *s) { return mb_text(2u, v, s); }

#ifdef MB_DIRTY
/* Probe (G1on family): the live patch (record rows + header, 264 B at
 * 0x1181B160) against the saved copy right after it, the OS's EDITED test.
 * Knobs 4-9: count of differing bytes, first offset, its saved > live
 * bytes, second offset, its bytes, last offset (hex; "-" none). */
#define MB_DLIVE 0x1181B160u
#define MB_DLEN  264u

#pragma CODE_SECTION(mb_hex, ".text")
#pragma FUNC_CANNOT_INLINE(mb_hex)
static int mb_hex(char *s, int n, uint32_t v, uint32_t d)
{
    uint32_t c;
#pragma UNROLL(1)
    while (d--) {
        c = v >> (4u * d) & 15u;
        s[n++] = (char)(c < 10u ? '0' + c : 'A' + c - 10u);
    }
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(mb_dtext, ".text")
#pragma FUNC_CANNOT_INLINE(mb_dtext)
static int mb_dtext(uint32_t k, char *s)
{
    const volatile uint8_t *a = (const volatile uint8_t *)(uintptr_t)MB_DLIVE, *b = a + MB_DLEN;
    uint32_t i, n = 0u, f0 = MB_DLEN, f1 = MB_DLEN, fl = MB_DLEN, o;
    int m;
#pragma UNROLL(1)
    for (i = 0u; i < MB_DLEN; i++)
        if (a[i] != b[i]) {
            n++;
            if (f0 == MB_DLEN) f0 = i;
            else if (f1 == MB_DLEN) f1 = i;
            fl = i;
        }
    if (k == 0u) { s[0] = 'N'; return mb_hex(s, 1, n, 3u); }
    o = k <= 2u ? f0 : k <= 4u ? f1 : fl;
    if (o == MB_DLEN) { s[0] = '-'; s[1] = 0; return 1; }
    if (k == 1u || k == 3u || k == 5u) { s[0] = '@'; return mb_hex(s, 1, o, 3u); }
    m = mb_hex(s, 0, b[o], 2u);
    s[m++] = '>';
    return mb_hex(s, m, a[o], 2u);
}

#pragma CODE_SECTION(GetString_MbP4, ".text")
int GetString_MbP4(unsigned int v, char *s) { (void)v; return mb_dtext(0u, s); }
#pragma CODE_SECTION(GetString_MbP5, ".text")
int GetString_MbP5(unsigned int v, char *s) { (void)v; return mb_dtext(1u, s); }
#pragma CODE_SECTION(GetString_MbP6, ".text")
int GetString_MbP6(unsigned int v, char *s) { (void)v; return mb_dtext(2u, s); }
#pragma CODE_SECTION(GetString_MbP7, ".text")
int GetString_MbP7(unsigned int v, char *s) { (void)v; return mb_dtext(3u, s); }
#pragma CODE_SECTION(GetString_MbP8, ".text")
int GetString_MbP8(unsigned int v, char *s) { (void)v; return mb_dtext(4u, s); }
#pragma CODE_SECTION(GetString_MbP9, ".text")
int GetString_MbP9(unsigned int v, char *s) { (void)v; return mb_dtext(5u, s); }
#endif

/* Storage knobs 4-9 (names Data1..Data6; the values are stored data):
 * DON'T / TURN / THESE on knobs 4-6 and again on 7-9 (one body per text:
 * the value doesn't say which knob). */
static const char mb_sdata[3][6] = { "DON'T", "TURN", "THESE" };

#pragma CODE_SECTION(mb_dstr, ".text")
#pragma FUNC_CANNOT_INLINE(mb_dstr)
static int mb_dstr(uint32_t i, char *s)
{
    int n = 0;
#pragma UNROLL(1)
    for (; mb_sdata[i][n]; n++) s[n] = mb_sdata[i][n];
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_MbData1, ".text")
int GetString_MbData1(unsigned int v, char *s) { (void)v; return mb_dstr(0u, s); }
#pragma CODE_SECTION(GetString_MbData2, ".text")
int GetString_MbData2(unsigned int v, char *s) { (void)v; return mb_dstr(1u, s); }
#pragma CODE_SECTION(GetString_MbData3, ".text")
int GetString_MbData3(unsigned int v, char *s) { (void)v; return mb_dstr(2u, s); }

typedef void (*MbWrite)(float *dst, float v, float k);

/* Edit handler of knobs 4-9 (linker Param.edit; A4 = state, B4 = column
 * 5..10). The OS has just written the turned value: put the shadow back
 * into the table and record rows, then the usual state[7] tail call. Only
 * while the shadow's slot still holds MelodyBox: an effect deleted before
 * it moves it, and the old slot then holds the next effect. */
#pragma FUNC_CANNOT_INLINE(Mb_LockEdit)
#pragma CODE_SECTION(Mb_LockEdit, ".text")
void Mb_LockEdit(uint32_t *state, uint32_t col)
{
    volatile MbState *st = mb_g;
    union { uint32_t u; float f; } k;
    if (st && st->magic0 == MB_MAGIC && st->magic1 == MB_MAGIC && st->lok && st->lfw &&
        col - MB_SCOL < MB_NS) {
        const ZmFw *t = &zm_fws[st->lfw - 1u];
        uint32_t c = col - MB_SCOL, v = st->sh[c], o = 44u * st->lslot + 4u * col;
        if (st->lslot < t->slots && ZM_U32(t->rec + 44u * st->lslot + 4u) == ZM_S_ID(MB_FXID)) {
            *(volatile uint32_t *)(uintptr_t)(t->tab + o) = v;
            *(volatile uint32_t *)(uintptr_t)(t->rec + o) = v;
            st->pend[c] = v;
        }
    }
    k.u = 0x44306666u;
    ((MbWrite)(uintptr_t)state[7])((float *)(uintptr_t)(state[1] + 12u + 4u * col), 0.0f, k.f);
}
