/*
 * SongBox -- chord song sequencer: bass and chords (electric piano or organ)
 * play a song edited on the pedal, DrumBox in the same patch follows it.
 *
 * Knobs: page 1 the song menu Bar / Chord / Rhythm, page 2 Rhy1..3 (the
 * pattern each rhythm plays, one of patterns/patterns.json, or Randm: a
 * pattern picked per run of bars with that rhythm, again on every pass),
 * page 3 Tempo
 * (40..250 BPM), Volume (50 = x1), Instr (sets.py INSTR_LIST: a chords
 * part and a bass part, either silent; drums follow in every mode).
 *
 * Sound: bass + keys (an instrument kit, sets.py + instruments/) play the saved song while the
 * footswitch is on (MENU PLAY: once or in a loop), added to the guitar.
 * Their 16ths swing like the DrumBox beat of the pattern.
 *
 * The song (up to 128 bars, chord + rhythm each) is kept in RAM and saved
 * in Floppy as one packed token stream (songpack.h, zmenu.h stream mode).
 * More copies of Floppy in the chain (up to 4) hold a longer stream: 106
 * bits + 102 per extra copy; zmenu finds them as parts 1..4. Every edit
 * changes the candidate list `cand` and (re)starts the packer, which runs a
 * few steps per audio call (TSCL budget). When it finishes: fits and
 * decodes back to `cand` -> committed (`bars`) and written to Floppy;
 * doesn't fit -> refused, the knobs jump back, Chord shows "Full" ("Err"
 * when the packed stream failed to decode back to `cand` 3 times). A song
 * that needs a Floppy that is missing (its stream stops inside an event, or
 * at the end of the parts present while a later part is there) can only be
 * cleared: other edits are refused with "Flop?".
 *
 * Bar: 0 = "MENU", 1..128 bars, then "STATUS"; the song's bars and the one
 * after its end (while the free bits can still hold a bar, 3 bits; without
 * Floppy up to 128 bars, kept in memory only) can be edited, STATUS follows
 * the last of them; Bar doesn't go past STATUS
 * (turned further, it jumps back). Rhythm shows "Rhy1".."Rhy3". On MENU Chord
 * picks an option and Rhythm shows its value; the three knob names spell the
 * option's question: PLAY OFF / ONCE / LOOP (Loop after a fresh start),
 * OCTAVE ("Shift chords by:") -1 / 0 / +1 (the chords one octave down / up), LENGTH ("Change
 * chord every:") BAR / HALF / BEAT (a song bar is 16 / 8 / 4 sixteenths; the rhythm
 * patterns and DrumBox still run in whole bars, a song ending inside one
 * rests up to its end; OCTAVE and LENGTH are saved with the song, 3 bits,
 * 6 for -1 or +1 with BEAT; PLAY isn't saved), TRIM NO / YES (YES counts down 5 s, "YES 5" .. "YES 1", then
 * removes the first END and every bar after it and shows DONE; NO or leaving
 * MENU stops it; the knob's third position snaps back to YES), CLEAR NO / YES
 * (the same, then empties the song). STATUS (read only): the knob names
 * show "Size:" "55/106" (used / capacity bits; "Pack" while packing) and the
 * playing bar "B: 12"; without Floppy the first two cycle per bar (~1.5 s
 * each when not playing) "Size: 55/0", "Can't save!", "(no floppy)". The
 * values: "STATUS", the playing chord "Am" and, after an arrow (font glyph
 * 0x1C), the next chord.
 * Chord: END, 24 chords (sharp names), Drums (bass and keys silent, the
 * drums play the bar's rhythm), Rest (silent). END on a bar of the song marks
 * the end: the song plays and is saved up to the first END; the END and the
 * bars after it stay only in memory (lost on a reload) until MENU TRIM
 * removes them, or until the END is turned back into a chord. The bar after
 * the end sits on END / "---"; a Chord turn there adds a bar with that
 * chord, a Rhythm turn a copy of the last bar with that rhythm.
 *
 * Code size: the linker calls .audio at its start, so every helper is
 * inlined; the big ones (packer start / step, unpacker, field jumps) have one
 * call site each, driven by request fields (sreq, lreq / vreq, jreq).
 */

#include <stdint.h>
#include <c6x.h>                        /* TSCL */

#define ZM_STREAM 1
#define ZM_DESC _Fx_SFX_SongBox_Desc
#define ZM_MAGIC 0x53505232u            /* 'SPR2' */
#define ZM_FLOPPY_FXID 522u
/* Code that runs rarely (loads, packing, edits, knob reads): one copy in
 * .text, called from the audio function, instead of inlined into it (the
 * linker resolves the calls; their loops stay interruptible). */
#define SS_DO_PRAGMA(x) _Pragma(#x)
#define SS_EXPAND_PRAGMA(x) SS_DO_PRAGMA(x)
#define SS_COLD(f) SS_EXPAND_PRAGMA(FUNC_CANNOT_INLINE(f)) SS_EXPAND_PRAGMA(CODE_SECTION(f, ".text")) SS_EXPAND_PRAGMA(FUNC_INTERRUPT_THRESHOLD(f, 1))
#define ZM_COLD(f) SS_COLD(f)
#define SP_COLD(f) SS_COLD(f)
#include "zmenu.h"
#include "songpack.h"
#include "songbox_sound.h"              /* patterns + note steps, from proto.py */
#include "songbox_drums.h"              /* per style: DrumBox beat, fills; swing amounts */

/* DrumBox control (any known OS, MS pedals untested; DrumBox's follow mode,
 * ../drumbox/drumbox.c): DrumBox (SFX + 520) in
 * another slot gets its knobs written into its state[31] table row only (the
 * patch record keeps the user's values; DrumBox's follow mode copies changed
 * columns into its params[]). Stopped: LenA "Song-" (= Skip), LenB Skip.
 * Playing: Tempo, LenB Skip, LenA "Song" (= 1 bar), BeatA / FillA of the current bar, from step
 * SS_DB_AHEAD on those of the next bar (DrumBox reads them at its bar line). */
#define SS_DB_ID    ((7u << 24) | 520u)
#define SS_ID       ((7u << 24) | 521u)  /* SongBox's own record word (a second copy, ss_second) */
#define SS_DB_AHEAD 12u
#define SS_DB_BEATA 0u                  /* DrumBox knob indices, 0-based */
#define SS_DB_FILLA 1u
#define SS_DB_LENA  2u
#define SS_DB_LENB  5u
#define SS_DB_TEMPO 6u                  /* BPM = 40 + value */
#define SS_DB_NONE  0xFFFFu
#define SS_DB_SONG_ON  90u              /* LenA markers (DrumBox DB_SONG_ON / _OFF): */
#define SS_DB_SONG_OFF 91u              /* 1 bar / Skip, shown "Song" / "Song-" */
#define SS_DB_SONG_CNT 92u              /* 1 bar played as count bars (hat quarters) */
#define SS_COUNT_BARS  2u              /* count-in after a patch change, with DrumBox */

#define SS_AUDIO_FUNC Fx_SFX_SongBox
ZM_EXPAND_PRAGMA(CODE_SECTION(SS_AUDIO_FUNC, ".audio"))
ZM_EXPAND_PRAGMA(FUNC_INTERRUPT_THRESHOLD(SS_AUDIO_FUNC, 1))

#define SS_MAGIC     0x53535541u        /* bump on a state layout change */
/* Patch change (a gap between audio calls, as DrumBox's DB_AWAY_CYC): the
 * song starts again at bar 1 after SS_AWAY_HOLD calls of silence, which also
 * gives DrumBox time for its cache check (else beat 1 would be dropped). */
#define SS_AWAY_CYC  9000000u           /* TSCL cycles, ~50 ms */
#define SS_AWAY_HOLD 256u               /* calls, ~46 ms */
/* The current patch number: zmenu's zm_fws[].patch (G1on: 0..99, B14 + 220;
 * MS-70CDR 2.10: 0..49, B14 + 324, stored before the patch apply, as
 * midistomp's CUR_PATCH; MS-50G 3.10 / MS-60B 2.10: B14 + 332; MS untested).
 * A patch change between two patches that both hold this effect leaves no
 * gap between calls, so it is detected by this number. */
#define SS_CYC       6000u              /* packer TSCL cycles per call (~18 %) */
#define SS_MAX_STEPS 8u                 /* also the limit when TSCL is stopped */
#define SS_FULL_CALLS 8000u             /* "Full" shown ~1.5 s */

#define SS_K_BAR    0u                  /* knob indices, 0-based */
#define SS_K_CHORD  1u
#define SS_K_RHYTHM 2u
#define SS_K_RHY1   3u                  /* Rhy1..3 = knobs 3..5 */
#define SS_K_TEMPO  6u
#define SS_TEMPO_MAX 210u               /* BPM = 40 + value (build.py's Tempo max) */
#define SS_K_VOLUME 7u
#define SS_K_INSTR  8u                  /* ss_instr[]: keys part << 4 | bass part */
#define SS_CLR_SEC  5513u               /* audio calls per second (44100 / 8) */
#define SS_CLR_CALLS (5u * SS_CLR_SEC)  /* Clear: "YES 5" .. "YES 1", then cleared */

/* One voice (inst_lib.py Voice). gate in 16ths, counts down. */
#define SS_VB 3u                        /* vk[SS_VB] = the bass voice */
typedef struct {
    float inc, p, p2, p3, env, menv, lp, lp2, bp, att, gate;
    uint32_t trig;
    uint32_t ip, ip2, ip3;              /* integer phases (2^32 = a cycle) */
} SsVoice;
/* voice d := voice s, field by field (ss_run's register copy; no memcpy) */
#define SS_VCOPY(d, s) ((d).inc = (s).inc, (d).p = (s).p, (d).p2 = (s).p2, (d).p3 = (s).p3, \
    (d).env = (s).env, (d).menv = (s).menv, (d).lp = (s).lp, (d).lp2 = (s).lp2, (d).bp = (s).bp, \
    (d).att = (s).att, (d).gate = (s).gate, (d).trig = (s).trig, (d).ip = (s).ip, (d).ip2 = (s).ip2, (d).ip3 = (s).ip3)
#include "songbox_instr.h"              /* instrument records, sets, Instr table, from sets.py */
#define SS_CH_END   0u                  /* Chord knob: END, 1..24 = chord + 1, Drums, Rest (last) */
#define SS_CH_DRUMS 25u
#define SS_CH_REST  26u
#define SS_B_END    25u                 /* END bar in cand / bars (never packed); SP_DRUMS 26 */

#define SS_NOPT     5u                  /* MENU options (0..LENGTH: values in ms[]): */
#define SS_OPT_PLAY 0u                  /* "Play": Rhythm OFF / ONCE / LOOP (not saved) */
#define SS_OPT_OCT  1u                  /* "Octave": the chords -1 / 0 / +1 octave (saved) */
#define SS_OPT_LEN  2u                  /* "Length": a song bar 16 / 8 / 4 steps (saved) */
#define SS_OPT_TRIM 3u                  /* "Trim": Rhythm NO / YES removes the first END and after */
#define SS_OPT_CLEAR 4u                 /* "Clear": NO / YES empties the song */
#define SS_PM_OFF   0u                  /* play modes */
#define SS_PM_ONCE  1u
#define SS_PM_LOOP  2u

/* All texts in one pool of NUL-terminated strings (ss_strs, string i =
 * ss_str(k, i)), <= 7 characters each:
 * - SS_S_KNOB: page 1 knob names Bar, Chord, Rhythm, then on MENU the
 *   names of knobs 1, 2, 3 per option, one word per knob (the OS centres
 *   each name over its knob, so words split across knobs don't join).
 *   STATUS builds its names at run time.
 * - SS_S_WORD: knob texts: MENU options 0..SS_NOPT - 1 (Chord knob on
 *   MENU), the fixed knob texts and STATUS's texts.
 * - SS_S_CHORD: chord names by bar code >> 1: roots 0..11, Rest 12,
 *   Drums 13.
 * - SS_S_INSTR: Instr's kit text parts (songbox_instr.h): "Mute", ""
 *   (bass alone), the chords part names, "Ch" (chords only), "+" + the bass
 *   part names; ss_text joins them. */
#define SS_S_KNOB   0u
#define SS_S_WORD   18u
#define SS_S_CHORD  48u
#define SS_S_INSTR  62u
#define SS_KNOB_NAMES_INIT \
    "Bar\0"    "Chord\0"  "Rhythm\0" \
    "Select\0" "play\0"   "mode:\0"         /* PLAY */ \
    "Shift\0"  "chords\0" "by:\0"           /* OCTAVE */ \
    "Change\0" "chord\0"  "every:\0"        /* LENGTH */ \
    "Trim\0"   "after\0"  "END?\0"          /* TRIM */ \
    "Clear\0"  "the\0"    "song?\0"         /* CLEAR */

/* Knob texts (SS_S_WORD + SS_W_*) */
#define SS_W_MENU   5u
#define SS_W_DASH3  6u
#define SS_W_END    7u
#define SS_W_FLOP   8u
#define SS_W_ERR    9u
#define SS_W_FULL   10u
#define SS_W_OFF    11u                 /* 3 values each for PLAY, OCTAVE, LENGTH */
#define SS_W_NO     20u
#define SS_W_YES    21u
#define SS_W_PACK   22u
#define SS_W_DONE   23u
#define SS_W_STATUS 24u
#define SS_W_SIZE   25u                 /* "Size:" */
#define SS_W_NOFL   26u                 /* "Can't" "save!", then "(no" "floppy)" */
#define SS_NWORDS   30u
#define SS_WORDS_INIT \
    "PLAY\0" "OCTAVE\0" "LENGTH\0" "TRIM\0" "CLEAR\0" \
    "MENU\0" \
    "---\0" "END\0" "Flop?\0" "Err\0" "Full\0" \
    "OFF\0" "ONCE\0" "LOOP\0" \
    "-1\0" "0\0" "+1\0" \
    "BAR\0" "HALF\0" "BEAT\0" \
    "NO\0" "YES\0" "Pack\0" \
    "DONE\0" "STATUS\0" "Size:\0" \
    "Can't\0" "save!\0" "(no  \0" "floppy)\0"
#define SS_CHORD_NAMES_INIT \
    "C\0" "C#\0" "D\0" "D#\0" "E\0" "F\0" \
    "F#\0" "G\0" "G#\0" "A\0" "A#\0" "B\0" \
    "Rest\0" "Drums\0"
#define SS_STRS_INIT SS_KNOB_NAMES_INIT SS_WORDS_INIT SS_CHORD_NAMES_INIT SS_INSTR_NAMES_INIT

/* All of SongBox's tables in one struct, so the linker's rela_const_base
 * gives every MVKL / MVKH of them one dynamic symbol (+ the member offset
 * as addend) instead of one per table. The generated headers give the
 * initializers; the old table names stay as aliases. */
typedef struct {
    uint32_t inst_pool[SS_NPOOL];
    uint32_t wt_terms[SS_WT_NTERMS];
    uint32_t pat_names[SS_NPAT + 1u];
    float swing[4];
    int8_t bass_ofs[SS_NBASS * 2u];
    uint8_t key_ofs[SS_NKEY * 6u];
    uint16_t pat[SS_NPAT * 4u];
    uint8_t pat_notes[SS_NNOTES];
    uint8_t db_style[SS_NPAT * 3u];
    uint8_t bar2[SS_NBAR2];
    uint8_t wt_end[SS_WT_NEND];
    uint8_t instr[SS_NINSTR];
    uint8_t keys_id[SS_NKEYI], bass_id[SS_NBASSI];    /* record | table << 4 */
    uint8_t keys_lvl[SS_NKEYI], bass_lvl[SS_NBASSI];  /* level * SS_LVL_DIV */
    uint8_t inst_idx[SS_NINSTB];
    char strs[sizeof(SS_STRS_INIT)];
    uint32_t inst_mask[SS_NREC];
} SsConst;
static const SsConst ss_c = {
    SS_INST_POOL_INIT, SS_WT_TERMS_INIT, SS_PAT_NAMES_INIT,
    SS_SWING_INIT, SS_BASS_OFS_INIT, SS_KEY_OFS_INIT, SS_PAT_INIT, SS_PAT_NOTES_INIT, SS_DB_STYLE_INIT, SS_BAR2_INIT, SS_WT_END_INIT, SS_INSTR_INIT,
    SS_KEYS_ID_INIT, SS_BASS_ID_INIT, SS_KEYS_LVL_INIT, SS_BASS_LVL_INIT, SS_INST_IDX_INIT, SS_STRS_INIT, SS_INST_MASK_INIT
};
/* SS_K: the tables' base. In the player code st->k (set each call): a load
 * from the state instead of an MVKL / MVKH pair with two .rela.dyn entries
 * per use; &ss_c where no state is at hand (the engine). */
#define SS_K (&ss_c)
#define ss_wt_terms    (SS_K->wt_terms)
#define ss_keys_lvl    (SS_K->keys_lvl)
#define ss_bass_lvl    (SS_K->bass_lvl)
#define ss_swing_amt   (SS_K->swing)
#define ss_bass_ofs    (SS_K->bass_ofs)
#define ss_key_ofs     (SS_K->key_ofs)
#define ss_pat         (SS_K->pat)
#define ss_pat_notes   (SS_K->pat_notes)
#define ss_db_style    (SS_K->db_style)
#define ss_bar2        (SS_K->bar2)
#define ss_wt_end      (SS_K->wt_end)
#define ss_instr       (SS_K->instr)
#define ss_keys_id     (SS_K->keys_id)
#define ss_bass_id     (SS_K->bass_id)
#define ss_pat_names   (SS_K->pat_names)

#include "instruments/inst_lib.h"
#include "instruments/engine.c"         /* the instrument engine: ss_wt_build, ss_run */
#undef SS_K
#define SS_K (st->k)

#define SS_PK_IDLE 0u
#define SS_PK_EDIT 1u                   /* write Floppy when it fits */
#define SS_PK_LOAD 2u                   /* only measure (loaded song) */
/* Capacity without Floppy: the most bits 128 bars can need, so Bar is never
 * limited. Per bar 3 (chord) + 6 (rhythm change) + flat / switch events
 * (6 each); for the best key those average at most 7 bits a bar (every bar
 * minor), 894 for 128 bars: 4 + 128 * 9 + 894. */
#define SS_NOFL_CAP 2050u


typedef struct {
    ZmStream zm;
    uint32_t magic;
    const SsConst *k;                   /* &ss_c (SS_K), set every call */
    uint32_t last[9];                   /* knob values as last seen */
    uint32_t sel, stat;                 /* Bar knob: 0 MENU, 1..128 bars, then STATUS; sel is STATUS */
    uint32_t opt;                       /* MENU option (Chord knob on MENU) */
    uint32_t n, cn, pn;                 /* committed / candidate bar count; played + saved (first END) */
    uint32_t used, key;                 /* bits of the committed stream, its key */
    uint32_t parts, cap, need;          /* Floppies used (>= 1), their bits; song needs a missing one */
    uint32_t sum, csum;                 /* checksums of bars / cand */
    uint32_t packing;                   /* SS_PK_* */
    uint32_t sreq, spn, sparts, spre;   /* start the packer on spsrc[0..spn), for sparts Floppies */
    const uint8_t *spsrc;
    uint32_t lreq, vreq;                /* unpack: Floppy load, verify a pack */
    uint32_t ereq, jreq;                /* pack the edited cand, field jump */
    uint32_t full, err;                 /* calls left showing "Full"; "Err" instead */
    uint32_t clr, clrd;                 /* Clear countdown (calls left); key of the MENU value shown */
    uint32_t cdone;                     /* CLEAR / TRIM finished: value shows DONE until touched */
    uint32_t blink;                     /* STATUS names: 0 "Can't save!", 1 "(no floppy)", 2 size */
    uint32_t nkey, ntick;               /* what the knob names show (last written); calls */
    uint32_t nk, npend;                 /* next knob name to write; names left to write */
    uint32_t play, bar, step, fire, arp;  /* player: song bar / 16th of the drum bar, events due, arp voice */
    uint32_t alast;                     /* the last arpeggio tone (MIDI) */
    uint32_t dbar;                      /* first song bar of the drum bar playing */
    uint32_t ms[3];                     /* MENU values: play mode SS_PM_*, octave, length */
    uint32_t fset;                      /* settings code on the Floppy (ss_ms_pre form) */
    uint32_t done;                      /* Once has played to the end */
    float stepPh;                       /* 16th phase 0..slen */
    float swing, slen;                  /* the drum bar's swing; this 16th's length (1 +- swing) */
    uint32_t swm;                       /* swing step bit: a step with step & swm lasts 1 - swing */
    uint32_t rseed, pass;               /* Randm: TSCL at the playback's start; passes through the song */
    SsVoice vk[4];                      /* keys 0..2, bass SS_VB */
    float acc[8];                       /* ss_play: this call's bass + keys samples */
    uint32_t si[16];                    /* ss_run's scratch (2 per sample) */
    float sf[24];
    SsInst inst[SS_NREC];               /* instrument records (unpacked by ss_wt_build) */
    uint32_t retry;                     /* packs again after a failed verify */
    uint32_t dslot, dtgt, dbeat, dfill; /* DrumBox: slot, bar whose values are written, they */
    uint32_t ddrv, dwait;               /* DrumBox driven (its live values are ours); join wait */
    uint32_t dv, dpos, dla;             /* fills: drum bars since the song start (wraps at pn), since
                                         * the run's start; this drum bar's look-ahead done */
    uint32_t cin, cpend;                /* count-in bars left; count-in at the next start */
    uint32_t lastCall, hold;            /* TSCL at the previous call; calls left stopped */
    uint32_t wtn;                       /* wavetable rows built (SS_WT_ROWS: ready) */
    uint32_t patch;                     /* patch number last seen (G1on) */
    uint32_t w[SP_NWORDS];
    uint8_t bars[SP_MAX_BARS], cand[SP_MAX_BARS];
    uint8_t tmp[SP_MAX_BARS], trh[SP_MAX_BARS];
    SpPack pk;
    uint32_t tail;                      /* SS_MAGIC again, the state's last word: another
                                         * effect's (smaller) state at the arena's end
                                         * overwrites it first */
} SsState;

static volatile SsState *ss_g = 0;      /* for the knob text callbacks */

#ifdef SS_PROBE
/* ------------------------------------------------------------------------
 * Load probe (build.py --probe, never a release build): TSCL cycles per
 * call of [0] the start up to zm_s_poll (checksums, OS check, slot scans),
 * [1] then up to the knob names (patch change, edits, packer, unpack,
 * jumps), [2] the knob names / STATUS texts, [3] the voice runs (ss_run,
 * all pieces), each as the peak and the busiest 4096-call mean, in % of
 * the time between calls. STATUS shows [0], [3], [2] as its knob names
 * "A12/9" "V25/11" "N6/5" (peak / mean; [1] not shown). Footswitch off clears them.
 * The meter sits just below the state.
 * ---------------------------------------------------------------------- */
#define SS_PM_MAGIC 0x50524F42u   /* 'PROB' */
#define SS_PM_BYTES (sizeof(SsMeter) + 8u)
#define SS_PM_N 4u

typedef struct {
    uint32_t magic;
    uint32_t period, perSum, perCnt;    /* ticks between calls, 4096-call mean */
    uint32_t cur[SS_PM_N];              /* this call's cycles */
    uint32_t shown[3];                  /* the shown percents per knob (redraw key) */
    uint32_t pk[SS_PM_N], av[SS_PM_N], win[SS_PM_N], winCnt;
} SsMeter;

#define SS_PM_OF(st) ((SsMeter *)(((uintptr_t)(st) - sizeof(SsMeter)) & ~(uintptr_t)7u))

/* end of a call: its costs into the peaks and windows (once the period is
 * known), cur cleared */
ZM_INLINE(ss_pm_end)
static inline void ss_pm_end(SsMeter *m)
{
    uint32_t k, c;
    for (k = 0u; k < SS_PM_N; ++k) {
        c = m->cur[k];
        m->cur[k] = 0u;
        if (m->period == 0u) continue;
        if (c > m->pk[k]) m->pk[k] = c;
        m->win[k] += c;
    }
    if (m->period != 0u && ++m->winCnt == 4096u) {
        for (k = 0u; k < SS_PM_N; ++k) {
            c = m->win[k] >> 12;
            if (c > m->av[k]) m->av[k] = c;
            m->win[k] = 0u;
        }
        m->winCnt = 0u;
    }
}

/* c cycles in % of the period (RCPSP + 2 Newton steps, no divide), 0..999 */
ZM_INLINE(ss_pm_pct)
static inline uint32_t ss_pm_pct(const SsMeter *m, uint32_t c)
{
    float p = (float)(int32_t)m->period, r, x;
    if (m->period == 0u) return 0u;
    r = _rcpsp(p);
    r = r * (2.0f - p * r);
    r = r * (2.0f - p * r);
    x = (float)(int32_t)c * 100.0f * r + 0.5f;
    return x > 999.0f ? 999u : (uint32_t)(int32_t)x;
}
#else
#define SS_PM_BYTES 0u
#endif

/* The instruments' wavetables (songbox_instr.h, SS_WT_WORDS words) below
 * the state (and the probe's meter). Built SS_WT_STEP rows per call
 * (ss_wt_build) while the song waits; again after a patch change (ctx[3] is
 * lent out to other patches). Each word reads as a tiny float. */
#define SS_WT_BYTES (SS_WT_WORDS * 4u)
#define SS_WT_OF(st) ((uint32_t *)(((uintptr_t)(st) - SS_PM_BYTES - SS_WT_BYTES) & ~(uintptr_t)7u))


SS_COLD(ss_sum)
static uint32_t ss_sum(const uint8_t *b, uint32_t n)
{
    uint32_t s = n * 0x9E3779B1u, i;
    for (i = 0u; i < n; ++i) s = s * 31u + b[i] + 1u;
    return s;
}

/* Rhythm state before bar q: the last chord or drums bar's rhythm (not a
 * rest or END), else 0. */
SS_COLD(ss_state_at)
static uint32_t ss_state_at(const uint8_t *b, uint32_t q)
{
    while (q > 0u) {
        q--;
        if (SP_HAS_RHY(b[q] & 31u)) return b[q] >> 5;
    }
    return 0u;
}

/* Index of the first END in b[0..n), else n: the bars played and saved. */
ZM_INLINE(ss_end)
static inline uint32_t ss_end(const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0u; i < n && (b[i] & 31u) != SS_B_END; ++i) {}
    return i;
}

/* Last bar that can be edited: min(128, cn + 1, length + free bits / 3)
 * (the song's bars, then one more while the bits hold it). */
ZM_INLINE(ss_limit)
static inline uint32_t ss_limit(uint32_t n, uint32_t used, uint32_t cap, uint32_t cn)
{
    uint32_t free = used < cap ? cap - used : 0u;
    uint32_t lim = n + ((free * 683u) >> 11);       /* free / 3, exact for free < 420 */
    if (lim > cn + 1u) lim = cn + 1u;
    return lim > SP_MAX_BARS ? SP_MAX_BARS : lim;
}

/* Clear countdown calls left -> seconds shown (5..1, 0 = not counting). */
ZM_INLINE(ss_clr_digit)
static inline uint32_t ss_clr_digit(uint32_t clr)
{
    uint32_t d = 0u, t = 0u;
    while (t < clr) { d++; t += SS_CLR_SEC; }
    return d;
}

/* Settings code for the packer (0..8, songpack.h): a + 3 * length, a = 0
 * for 0, 1 for +1, 2 for -1, so 0 = the defaults (0, BAR) and -1 / +1 with
 * BEAT (7, 8) take the escape. */
ZM_INLINE(ss_ms_pre)
static inline uint32_t ss_ms_pre(const SsState *st)
{
    uint32_t o = st->ms[SS_OPT_OCT];
    return (o == 1u ? 0u : o == 2u ? 1u : 2u) + 3u * st->ms[SS_OPT_LEN];
}

/* OCTAVE and LENGTH from a settings code c (0..8). */
SS_COLD(ss_ms_set)
static void ss_ms_set(SsState *st, uint32_t c)
{
    uint32_t len = c >= 6u ? 2u : c >= 3u ? 1u : 0u, a = c - 3u * len;
    st->ms[SS_OPT_OCT] = a == 0u ? 1u : a == 1u ? 2u : 0u;
    st->ms[SS_OPT_LEN] = len;
    st->fset = c;
}

/* Pack cand. */
SS_COLD(ss_request_pack)
static void ss_request_pack(SsState *st, uint32_t mode)
{
    st->csum = ss_sum(st->cand, st->cn);
    st->vreq = 0u;                      /* a stale result is never saved */
    st->retry = 0u;
    st->spsrc = st->cand;
    st->spn = st->cn;                   /* cut at the first END where the packer starts */
    st->sparts = st->parts;
    st->spre = ss_ms_pre(st);
    st->sreq = 1u;
    st->packing = mode;
}

SS_COLD(ss_fresh)
static inline void ss_fresh(SsState *st, unsigned int *ctx)
{
    float *params = (float *)(uintptr_t)ctx[1];
    uint32_t i;
    /* all of it: another effect's state (DrumBox's) may have used this
     * arena, and its magic must not survive in a field we never write */
    for (i = 0u; i < sizeof(SsState) / 4u; ++i) ((volatile uint32_t *)st)[i] = 0u;
    st->magic = SS_MAGIC;
    st->tail = SS_MAGIC;
    for (i = 0u; i < 9u; ++i) st->last[i] = zm_knob(params[5u + i], 150u);
    st->sel = st->last[SS_K_BAR]; st->stat = 0u;
    st->opt = 0u;
    st->n = 0u; st->cn = 0u; st->pn = 0u;
    st->sum = 0u; st->csum = 0u;        /* ss_sum of an empty list */
    st->used = SP_KEY_BITS; st->key = 0u;
    st->parts = 1u; st->cap = SP_CAP_BITS; st->need = 0u;
    st->packing = SS_PK_IDLE;
    st->lreq = 0u; st->vreq = 0u; st->sreq = 0u; st->ereq = 0u; st->jreq = 0u;
    st->full = 0u; st->err = 0u; st->retry = 0u;
    st->clr = 0u; st->clrd = 0u; st->cdone = 0u;
    st->play = 0u; st->fire = 0u;
    st->lastCall = 0u; st->hold = 0u; st->patch = 0xFFFFFFFFu; st->wtn = 0u;
    st->ms[SS_OPT_PLAY] = SS_PM_LOOP; st->ms[SS_OPT_OCT] = 1u; st->ms[SS_OPT_LEN] = 0u;
    st->fset = 0u; st->done = 0u; st->dbar = 0u;
    for (i = 0u; i < sizeof(SsVoice) * 4u; ++i) ((volatile uint8_t *)st->vk)[i] = 0u;
    st->dslot = ZM_NOSLOT; st->dtgt = SS_DB_NONE; st->ddrv = 0u; st->dwait = 0u;
    st->cin = 0u; st->cpend = 0u;
    st->zm.magic = 0u;                  /* re-read Floppy */
}

/* The packer finished. */
SS_COLD(ss_pack_done)
static void ss_pack_done(SsState *st)
{
    uint32_t ok, i;
    /* a song that needs a missing Floppy: only clearing it is saved */
    ok = st->packing == SS_PK_EDIT && st->need && st->cn;
    if (st->pk.fits && st->retry < 3u && !ok) {
        st->vreq = 1u;                  /* never save what doesn't decode to cand */
        return;
    }
    if (st->packing == SS_PK_EDIT && !st->zm.nparts) {     /* no Floppy: the edit stays in RAM */
        for (i = 0u; i < st->cn; ++i) st->bars[i] = st->cand[i];
        st->n = st->cn;
        st->pn = st->spn;
        st->sum = st->csum;
        st->need = 0u;
        st->key = st->pk.bkey;          /* the notes' key check (ss_key_mask) */
        st->used = SP_KEY_BITS + SP_BUDGET * SP_MAX_PARTS;     /* more than 4 Floppies hold: at least that */
    } else if (st->packing == SS_PK_EDIT) {     /* refused (doesn't fit, never verified, Floppy missing) */
        for (i = 0u; i < st->n; ++i) st->cand[i] = st->bars[i];
        st->cn = st->n;
        st->csum = st->sum;
        ss_ms_set(st, st->fset);        /* MENU values back to the Floppy's */
        st->full = SS_FULL_CALLS;
        st->err = ok ? 2u : st->pk.fits;    /* 1: fits but never verified */
        st->jreq = 1u;
    } else {
        st->used = st->cap;             /* loaded song the packer can't fit */
    }
    st->packing = SS_PK_IDLE;
}

/* The one unpack of this call: a Floppy load first, then a verify. */
SS_COLD(ss_unpack_job)
static void ss_unpack_job(SsState *st)
{
    const uint32_t *src;
    uint32_t req, n, ok, i, parts, how, set;
    if (st->lreq) { req = 1u; st->lreq = 0u; src = st->w; parts = st->parts; }
    else { req = 2u; st->vreq = 0u; src = st->pk.words; parts = st->pk.parts; }
    n = sp_unpack(src, st->tmp, st->trh, parts);
    how = (n >> 8) & 3u;
    set = n >> 16;
    n &= 255u;
    if (req == 1u) {      /* song from Floppy (patch load, Floppy added) */
        for (i = 0u; i < n; ++i) { st->bars[i] = st->tmp[i]; st->cand[i] = st->tmp[i]; }
        st->n = n;
        st->cn = n;
        st->pn = n;
        st->used = st->cap;             /* until the packer has measured it */
        st->key = st->w[0] & 15u;
        /* cut inside an event, or ran out with a later part there */
        st->need = how == SP_END_CUT || (how == SP_END_OUT && st->zm.gap);
        st->full = 0u;
        ss_ms_set(st, set);             /* saved OCTAVE, LENGTH */
        ss_request_pack(st, SS_PK_LOAD);
        st->sum = st->csum;
        st->jreq = 1u;
    } else {                            /* verify the packer's result (for the Floppies there now) */
        ok = n == st->spn && st->pk.n == st->spn && st->pk.parts == st->parts && set == st->pk.pre;
        for (i = 0u; ok && i < n; ++i) ok = st->tmp[i] == st->cand[i];
        if (!ok) {                      /* state disturbed (or a packer bug): pack again, */
            st->retry++;                /* 3 failures = refused */
            st->spsrc = st->cand; st->spn = st->cn; st->sparts = st->parts; st->sreq = 1u;
            return;
        }
        st->used = st->pk.bits;
        st->key = st->pk.bkey;
        if (st->packing == SS_PK_EDIT) {
            for (i = 0u; i < st->cn; ++i) st->bars[i] = st->cand[i];
            st->n = st->cn;             /* with the END and the bars after it */
            st->pn = st->spn;
            st->sum = st->csum;
            st->need = 0u;              /* the whole song is in the parts now */
            st->fset = set;
            zm_s_put(&st->zm, st->pk.words);
        }
        st->packing = SS_PK_IDLE;
    }
}

SS_COLD(ss_edit)
static void ss_edit(SsState *st, unsigned int *ctx)
{
    float *params = (float *)(uintptr_t)ctx[1];
    uint32_t sel, v, idx, lim, f, b, e;

    /* MENU CLEAR / TRIM's value jumps to last[Rhythm] (NO / YES), see jreq.
     * e = what stays: CLEAR nothing, TRIM the bars before the first END. */
    e = 0u;
    if (st->sel == 0u && st->opt == SS_OPT_TRIM) e = ss_end(st->cand, st->cn);
    if (st->clr) {                      /* countdown ran out: empty the song (cut at the first END), NO */
        st->clr--;
        if (st->clr == 0u) {
            st->cn = e;
            st->ereq = 1u; st->jreq = 1u; st->last[SS_K_RHYTHM] = 0u;
            st->cdone = 1u;
            return;                     /* the knob still reads YES until the jump: not a new turn */
        }
    }
    sel = zm_knob(params[5u + SS_K_BAR], SP_MAX_BARS + 1u);
    lim = ss_limit(st->n, st->used, st->cap, st->cn);
    if (sel > lim + 1u) {               /* past STATUS: Bar jumps back to it */
        if (st->lreq) return;           /* song not loaded yet, lim not known */
        sel = lim + 1u;
        st->jreq = 1u;
    }
    st->stat = sel > lim;               /* STATUS: read only */
    if (sel != st->sel) {
        st->sel = sel;
        st->clr = 0u;
        st->cdone = 0u;
        st->last[SS_K_BAR] = sel;
        st->last[SS_K_RHYTHM] = 0u;     /* MENU: shows NO */
        st->full = 0u;
        st->jreq = 1u;
        return;
    }
    if (st->stat) return;
    idx = sel - 1u;

    v = zm_knob(params[5u + SS_K_CHORD], SS_CH_REST);
    if (v != st->last[SS_K_CHORD]) {
        st->last[SS_K_CHORD] = v;
        if (sel == 0u) {                                /* MENU: pick an option */
            st->opt = v < SS_NOPT ? v : SS_NOPT - 1u;
            st->clr = 0u;
            st->cdone = 0u;
            st->last[SS_K_RHYTHM] = 0u;
            st->jreq = 1u;                              /* Chord to it, value to No */
            return;
        }
        f = v == SS_CH_DRUMS ? SP_DRUMS : v - 1u;      /* bar code of a chord or Drums */
        if (idx < st->cn) {
            if (v == SS_CH_END) b = SS_B_END;
            else if (v == SS_CH_REST) b = SP_REST;
            else if (!SP_HAS_RHY(st->cand[idx] & 31u)) b = f | ss_state_at(st->cand, idx) << 5;
            else b = f | (uint32_t)(st->cand[idx] >> 5) << 5;
            if (b != st->cand[idx]) { st->cand[idx] = (uint8_t)b; st->ereq = 1u; }
        } else if (v != SS_CH_END) {
            /* the bar after the end (idx == cn): a new bar with that chord */
            st->cand[idx] = (uint8_t)(v == SS_CH_REST ? SP_REST : f | ss_state_at(st->cand, idx) << 5);
            st->cn = idx + 1u;
            st->ereq = 1u;
        }
        return;
    }

    v = zm_knob(params[5u + SS_K_RHYTHM], 2u);
    if (v != st->last[SS_K_RHYTHM]) {
        if (sel == 0u && st->opt >= SS_OPT_TRIM && v > 1u) {        /* TRIM, CLEAR */
            v = 1u;                                     /* NO / YES / YES: snap back to YES */
            st->jreq = 1u;
        }
        st->last[SS_K_RHYTHM] = v;
        st->cdone = 0u;
        if (sel == 0u) {                                /* MENU option value */
            if (st->opt < SS_OPT_TRIM) {                /* PLAY, OCTAVE, LENGTH */
                st->ms[st->opt] = v;
                if (st->opt == SS_OPT_PLAY) {           /* new mode: start again at bar 1 */
                    st->done = 0u;
                    st->play = 0u;
                } else {
                    /* saved with the song: pack it again (refused -> the Floppy's values) */
                    st->ereq = 1u;
                }
            } else {                                    /* TRIM, CLEAR: YES starts the countdown, NO stops it */
                if (v == 0u) st->clr = 0u;              /* (only when there is something to remove) */
                else if (st->clr == 0u && st->cn > e) st->clr = SS_CLR_CALLS;
            }
            return;
        }
        if (idx < st->cn) {
            b = st->cand[idx];
            if (!SP_HAS_RHY(b & 31u)) return;           /* a rest or END has no rhythm */
            b = (b & 31u) | v << 5;
            if (b != st->cand[idx]) { st->cand[idx] = (uint8_t)b; st->ereq = 1u; }
        } else {                                        /* the bar after the end */
            f = st->cn ? st->cand[st->cn - 1u] : 0u;    /* C, rhythm 1 for an empty song */
            if ((f & 31u) == SS_B_END) return;          /* after an END: no copy */
            st->cand[idx] = (uint8_t)((f & 31u) == SP_REST ? SP_REST : (f & 31u) | v << 5);
            st->cn = idx + 1u;
            st->ereq = 1u;
            st->jreq = 1u;                              /* Chord shows the copied chord */
        }
    }
}

/* ------------------------------------------------------------- player
 * Mirror of proto.py's renderer (16th steps by a phase accumulator
 * instead of whole samples per step); the instruments in instruments/. */

SS_COLD(ss_trigger)
static void ss_trigger(SsVoice *v, uint32_t note, float gate)
{
    float inc = SS_INC0;
    while (note >= 12u) { note -= 12u; inc *= 2.0f; }
    for (; note; --note) inc *= SS_SEMI;    /* equal temperament, as proto.py's FREQ */
    v->inc = inc;
    v->gate = gate;
    v->trig = 1u;
}

/* Style (pattern index) of bar q, a chord or drums bar of the song: its
 * rhythm's Rhy knob, 1..SS_NPAT = pattern + 1, 0 = Randm: one of the first
 * SS_NRAND patterns per run of bars with this rhythm (a rest, another
 * rhythm or bar 1 starts a run), the same for every bar of the run, new
 * for each run and each pass w through the song (and each playback,
 * rseed); a function of (rseed, w, run start), so the player, the drums'
 * look-ahead (w + 1 across the wrap) and the swing agree. Integer only
 * (as DrumBox's db_pick_beat). */
SS_COLD(ss_style)
static uint32_t ss_style(const SsState *st, const float *params, uint32_t q, uint32_t w)
{
    uint32_t rh = st->bars[q] >> 5, v = zm_knob(params[5u + SS_K_RHY1 + rh], SS_NPAT), h;
    if (v) return v - 1u;
    while (q > 0u && (uint32_t)(st->bars[q - 1u] >> 5) == rh && SP_HAS_RHY(st->bars[q - 1u] & 31u)) q--;
    h = st->rseed ^ (q << 2 | rh) * 0x9E3779B1u ^ w * 0x85EBCA6Bu;
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return ((h >> 16) * SS_NRAND) >> 16;
}

/* The swing of the drum bar from dbar (as DrumBox takes it from its beat
 * at its bar line): its first song bar's style; straight in a count-in or
 * when that bar is a rest (DrumBox plays None, straight). */
SS_COLD(ss_swing)
static void ss_swing(SsState *st, const float *params)
{
    uint32_t q = st->dbar, c = 0u;
    if (q < st->pn && (st->bars[q] & 31u) != SP_REST && !st->cin)
        c = ss_pat[ss_style(st, params, q, st->pass) * 4u + 3u] >> 14;
    st->swing = ss_swing_amt[c];
    st->swm = SS_SWING_MASK(c);
}

/* Pitch classes (bit = semitone) of the song's key: the packer's key
 * (st->key, the one whose chord set fits the song best) as a major scale;
 * when the first chord bar is the key's vi (Am in C: a minor song) also
 * the vi's leading tone (G#), for the major V chord. */
SS_COLD(ss_key_mask)
static uint32_t ss_key_mask(const SsState *st)
{
    uint32_t k = st->key < 12u ? st->key : 0u, m = 0xAB5u << k, i, c;
    m = (m | m >> 12) & 0xFFFu;
    for (i = 0u; i < st->pn && (st->bars[i] & 31u) >= SP_REST; ++i) {}
    c = k + 9u;
    if (c >= 12u) c -= 12u;
    if (i < st->pn && (st->bars[i] & 31u) == (c << 1 | 1u)) {
        c = k + 8u;
        if (c >= 12u) c -= 12u;
        m |= 1u << c;
    }
    return m;
}

/* 1 when pitch class p (0..25) is in key mask m. */
ZM_INLINE(ss_in_key)
static inline uint32_t ss_in_key(uint32_t m, uint32_t p)
{
    while (p >= 12u) p -= 12u;
    return (m >> p) & 1u;
}

/* ss_events: triad tone c as t when it isn't the last arpeggio tone and is
 * nearer to pat than the best so far (distance in minor). */
#define SS_NEAR(c) do { if ((c) != st->alast) { b = (c) > pat ? (c) - pat : pat - (c); \
    if (b < minor) { minor = b; t = (c); } } } while (0)

/* Keys tone n (MIDI) of offset o above root: folded into 55..66 (G3..F#4)
 * with fh 12, fl 55; unfolded (48 + root + o) with fh 99, fl 0. */
#define SS_KEY_TONE(n, o) do { (n) = root + (o); if ((n) >= fh) (n) -= 12u; \
    (n) += 48u; if ((n) < fl) (n) += 12u; } while (0)

/* Events of st->bar / st->step: bass and keys from the bar's pattern (step
 * = the 16th of the drum bar, so a short song bar plays part of it). Note
 * codes are relative to the bar's chord (song_patterns.py: BASS_OFS,
 * KEY_OFS). */
SS_COLD(ss_events)
static inline void ss_events(SsState *st, const float *params)
{
    uint32_t b = st->bar < st->pn ? st->bars[st->bar] : SP_REST, root, minor, pat, code, i, t, n0, n1, n2, x, km;
    uint32_t fh = 12u, fl = 55u;        /* SS_KEY_TONE's fold */
    float len;
    if ((b & 31u) >= SP_REST) {         /* rest, drums bar (or past the end): bass and keys released */
        if ((st->step & (15u >> st->ms[SS_OPT_LEN])) == 0u) {
            for (i = 0u; i < 4u; ++i) st->vk[i].gate = 0.0f;
        }
        return;
    }
    root = (b & 31u) >> 1;
    minor = b & 1u;
    pat = ss_style(st, params, st->bar, st->pass) * 4u;
    /* lane = onset mask (bit = step) + the offset of its notes in
     * ss_pat_notes (the keys offset carries the swing code in bits 14-15) */
    i = st->step;
    code = 0u;
    #pragma UNROLL(1)
    for (n0 = 0u; n0 < 2u; ++n0) {      /* code = bass note, then keys note */
        t = ss_pat[pat + n0];
        x = ss_pat[pat + 2u + n0] & 0x0FFFu;   /* (bits 12-15 bar2, 14-15 swing) */
        n1 = code;
        code = (t >> i) & 1u ? ss_pat_notes[x + SS_BITS(t & ((1u << i) - 1u))] : 0u;
    }
    pat = code;                         /* keys note */
    code = n1;                          /* bass note */
    km = ss_key_mask(st);
    /* 6th, b7, 7th, 2nd outside the song's key: the root */
    if ((code & 15u) - 4u < 4u && !ss_in_key(km, root + (uint32_t)ss_bass_ofs[(code & 15u) * 2u + minor]))
        code &= ~15u;
    if (code) {
        t = root;
        if ((code & 15u) == SS_BASS_APPROACH) {     /* a half step below the next bar's root */
            x = st->bar + 1u < st->pn ? st->bars[st->bar + 1u] & 31u : st->bars[0] & 31u;
            if (x < SP_REST) t = x >> 1;
        }
        t += 8u;                        /* 28 + (root - 4) mod 12: E1..D#2, + the code's interval */
        if (t >= 12u) t -= 12u;
        t = (uint32_t)((int32_t)(t + 28u) + ss_bass_ofs[(code & 15u) * 2u + minor]);
        ss_trigger(&st->vk[SS_VB], t, (float)(code >> 4) - 0.25f);
    }
    code = pat;                         /* keys note */
    if (!code) return;
    /* a tone neither in the triad nor in the song's key (sus4 on IV, sus2
     * on iii or a minor song's V, ...): the triad; a single tone: pat =
     * the replaced tone, played as the triad tone nearest to it (below);
     * the rooted codes are chord tones */
    pat = 0u;
    if ((code & 15u) - 4u < SS_KEY_ROOTED - 4u) {
        x = (code & 15u) * 6u + minor * 3u;
        n1 = 0u;
        #pragma UNROLL(1)
        for (i = 0u; i < 3u; ++i) {
            t = ss_key_ofs[x + i];
            if (t != 0u && t != 7u && t != 4u - minor && !ss_in_key(km, root + t)) n1 = 1u;
        }
        if (n1 && (code & 15u) >= SS_KEY_SINGLE) SS_KEY_TONE(pat, ss_key_ofs[x]);
        if (n1) code = (code & ~15u) | ((code & 15u) >= SS_KEY_SINGLE ? 1u : 0u);
    }
    /* the code's three tones folded into 55..66 (G3..F#4), sorted, then
     * OCTAVE; the rooted codes unfolded above the root at C3..B3 (their
     * offsets ascend, so the sort keeps them) */
    if ((code & 15u) >= SS_KEY_ROOTED) { fh = 99u; fl = 0u; }
    x = (code & 15u) * 6u + minor * 3u;
    SS_KEY_TONE(n0, ss_key_ofs[x]);
    SS_KEY_TONE(n1, ss_key_ofs[x + 1u]);
    SS_KEY_TONE(n2, ss_key_ofs[x + 2u]);
    if (n0 > n1) { x = n0; n0 = n1; n1 = x; }
    if (n1 > n2) { x = n1; n1 = n2; n2 = x; }
    if (n0 > n1) { x = n0; n0 = n1; n1 = x; }
    x = st->ms[SS_OPT_OCT] * 12u - 12u;    /* -1 / 0 / +1: -12 / 0 / +12 */
    n0 += x; n1 += x; n2 += x;
    len = (float)(code >> 4);
    if ((SS_KEY_CHORDS >> (code & 15u)) & 1u) {     /* chord: all three voices */
        ss_trigger(&st->vk[0], n0, len - 0.25f);
        ss_trigger(&st->vk[1], n1, len - 0.25f);
        ss_trigger(&st->vk[2], n2, len - 0.25f);
    } else {                            /* arpeggio: one tone (1 / 2 / 3 the triad's low / mid /
                                         * high; single-tone codes: all three equal), voices in turn */
        for (i = 0u; i < 3u; ++i) st->vk[i].gate = 0.0f;
        if (st->step == 0u) st->arp = 0u;
        t = (code & 15u) == 2u ? n1 : (code & 15u) == 3u ? n2 : n0;
        if (pat) {                      /* a replaced tone: the triad tone nearest to it, not the last one */
            pat += x;
            minor = 99u;
            SS_NEAR(n0);
            SS_NEAR(n1);
            SS_NEAR(n2);
        }
        ss_trigger(&st->vk[st->arp], t, len);
        st->alast = t;
        st->arp = st->arp < 2u ? st->arp + 1u : 0u;
    }
}

/* DrumBox knob k := v in its table row, and with rec in its patch record
 * row too (written only when it differs). */
SS_COLD(ss_db_col)
static void ss_db_col(const ZmFw *t, uint32_t slot, uint32_t k, uint32_t v, uint32_t rec)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(t->tab + 44u * slot + 4u * (2u + k));
    if (*p != v) *p = v;
    p = (volatile uint32_t *)(uintptr_t)(t->rec + 44u * slot + 4u * (2u + k));
    if (rec && *p != v) *p = v;
}

/* 1 when a SongBox sits in an earlier slot: this one is a second copy
 * (both would read and write the same Floppy and DrumBox; also while the
 * effect list is scrolled past SongBox), so it stays passive: the guitar
 * passes, nothing is written. The first slot wins (slot records: seen by
 * every copy, whether or not they share code). Also 1 when the own slot's
 * record no longer holds SongBox (being replaced in the effect list: the
 * new effect's id may be there before the last call; ss_drums would take
 * it for a DrumBox to drive). */
ZM_INLINE(ss_second)
static inline uint32_t ss_second(const SsState *st)
{
    const ZmFw *t = zm_s_fw(&st->zm);
    uint32_t s, m, r = 0u;
    if (t && st->zm.slot < t->slots)
        for (s = 0u; s <= st->zm.slot; ++s) {
            m = ZM_U32(t->rec + 44u * s + 4u) == SS_ID;
            if (m != (s == st->zm.slot)) r = 1u;
        }
    return r;
}

/* Find DrumBox; write its knobs for the player's state (see SS_DB_ID). */
SS_COLD(ss_drums)
static inline void ss_drums(SsState *st, const float *params)
{
    const ZmFw *t = zm_s_fw(&st->zm);  /* own rows of the known OS, 0 = none */
    uint32_t s, ds = ZM_NOSLOT, b, nb, tg, rh, sty, q, la, k, e, w, beat = 0u, fill = 0u;
    /* the record id test as for Floppy: (gid << 24) | fxid on every known OS */
    if (t)
        for (s = 0u; s < t->slots; ++s)
            if (ZM_U32(t->rec + 44u * s + 4u) == SS_DB_ID) { ds = s; break; }
    st->dslot = ds;                     /* one store: the knob text reads it */
    if (ds == ZM_NOSLOT) {
        st->ddrv = 0u;
        return;
    }
    /* No song (an empty Floppy, or this effect only passing by while the
     * effect list is scrolled): leave DrumBox alone, and once give it back
     * its own values (table row BeatA..LenB := its patch record; ours went
     * only into the table) so it plays on its own again. */
    if (st->pn == 0u) {
        if (st->ddrv) {
            volatile uint32_t *tb = (volatile uint32_t *)(uintptr_t)(t->tab + 44u * ds + 8u);
            const volatile uint32_t *rc = (const volatile uint32_t *)(uintptr_t)(t->rec + 44u * ds + 8u);
            for (s = 0u; s <= SS_DB_LENB; ++s) tb[s] = rc[s];
            st->ddrv = 0u;
        }
        return;
    }
    /* Found while the song plays (added to the chain, or the song came
     * back): keep it stopped until our next bar line, then start it there,
     * so its bars line up with ours (2: wait for a step past 0, 1: for
     * step 0 again; the bar began within the last call). Not when the song
     * starts in this call (fire set): DrumBox starts with it. */
    if (!st->ddrv) st->dwait = (st->play && !st->fire) ? 2u : 0u;
    st->ddrv = 1u;
    if (st->dwait == 2u && st->step != 0u) st->dwait = 1u;
    if (st->dwait == 1u && st->step == 0u) st->dwait = 0u;
    if (!st->play || st->dwait) {       /* stopped (or waiting): no drums */
        st->dtgt = SS_DB_NONE;
        ss_db_col(t, st->dslot, SS_DB_LENA, SS_DB_SONG_OFF, 0u);
        ss_db_col(t, st->dslot, SS_DB_LENB, 0u, 0u);
        return;
    }
    /* A drum bar holds k = 1 << LENGTH song bars, the playing one from dbar
     * on. Count-in: dbar stays 0, so its last bar's look-ahead is bar 0.
     * Fills count drum bars (dv, dpos), so they come as often at every
     * LENGTH as at BAR (k = 1: dv = tg): the end fill at drum bar pn of the
     * song, the middle fill at drum bar 4 of every 8 of a run. */
    k = 1u << st->ms[SS_OPT_LEN];
    q = st->dbar < st->pn ? st->dbar : 0u;
    tg = (st->step < SS_DB_AHEAD || st->cin) ? q : (q + k < st->pn ? q + k : 0u);
    /* tg's pass: the next one when the look-ahead wraps to bar 1 */
    w = st->pass + (st->step >= SS_DB_AHEAD && !st->cin && q + k >= st->pn);
    la = st->step >= SS_DB_AHEAD;
    if (!la) st->dla = 0u;
    la &= !(st->cin | st->dla);         /* the next drum bar's look-ahead (pn <= k: the same bar) */
    if (tg != st->dtgt || la) {
        s = st->dv + la;
        st->dv = s < st->pn ? s : 0u;
        st->dpos += la;
        if (st->dtgt == SS_DB_NONE) st->dv = tg, st->dpos = 0u;   /* start (or joined at bar tg) */
        st->dla |= la;
        st->dtgt = tg;
        e = tg + k < st->pn ? tg + k : st->pn;      /* the next drum bar's first song bar */
        /* the drum bar's first song bar decides (DrumBox changes only at
         * its bar lines): a rest -> beat None, no fill (silent; a "Drums"
         * bar keeps the drums), else its rhythm */
        q = (st->bars[tg] & 31u) == SP_REST ? e : tg;
        if (q < e) {
            b = st->bars[q];
            rh = b >> 5;
            sty = ss_style(st, params, q, w);
            beat = ss_db_style[sty * 3u];
            s = e < st->pn ? e : 0u;
            nb = st->bars[s];
            /* the run ends: last bar, a rest, another rhythm, another style
             * (a Randm run at the song's wrap) */
            if (st->dv + 1u >= st->pn || (e >= st->pn && st->ms[SS_OPT_PLAY] != SS_PM_LOOP)
                || (nb & 31u) == SP_REST || (nb >> 5) != rh || ss_style(st, params, s, w + (e >= st->pn)) != sty) {
                fill = ss_db_style[sty * 3u + 2u];           /* end fill */
                q = e;                                  /* the next drum bar starts a run */
            } else if ((st->dpos & 7u) == 3u) {         /* middle fill: drum bar 4 of every 8 of the run */
                fill = ss_db_style[sty * 3u + 1u];
            } else if (st->dpos & 1u) {                 /* every second drum bar: the bar2 fill (0 none) */
                fill = ss_bar2[ss_pat[sty * 4u + 2u] >> 12];
            }
        }
        if (q >= e) st->dpos = ~0u;     /* after an end fill or a rest */
        st->dbeat = beat;
        st->dfill = fill;
    }
    /* Tempo also in the record: DrumBox's page shows it, a save keeps it */
    ss_db_col(t, st->dslot, SS_DB_TEMPO, zm_knob(params[5u + SS_K_TEMPO], SS_TEMPO_MAX), 1u);
    ss_db_col(t, st->dslot, SS_DB_LENB, 0u, 0u);
    ss_db_col(t, st->dslot, SS_DB_BEATA, st->dbeat, 0u);
    ss_db_col(t, st->dslot, SS_DB_FILLA, st->dfill, 0u);
    /* last: from Skip, DrumBox starts now; count-in until the last count
     * bar's look-ahead (step 12), so its next bar line is the song's bar 1 */
    ss_db_col(t, st->dslot, SS_DB_LENA,
              st->cin > (uint32_t)(st->step >= SS_DB_AHEAD) ? SS_DB_SONG_CNT : SS_DB_SONG_ON, 0u);
}

/* 8 samples of bass + keys added to both channels of ctx[5]. The sequencer
 * steps sample by sample; the voices run in pieces between its events
 * (voice by voice, ss_run: the instrument picked once per piece, the voice
 * in registers), summed in st->acc in the voice order of one sample at a
 * time, so the sound is the same as voice-by-voice per sample. A 16th is
 * thousands of samples long: at most one event per call. */
ZM_INLINE(ss_play)
static inline void ss_play(SsState *st, unsigned int *ctx)
{
    float *params = (float *)(uintptr_t)ctx[1];
    float *fx = (float *)(uintptr_t)ctx[5];
    float inc, vol, s, bg, kg, len;
    uint32_t i, j, ki, bi, lm, i0, e, ev;

    if (params[0] < 0.5f) st->done = 0u;            /* footswitch off: Once plays again */
    if (st->hold) st->hold--;                       /* after a patch change */
    if (params[0] < 0.5f || st->pn == 0u || st->ms[SS_OPT_PLAY] == SS_PM_OFF || st->done || st->hold
        || st->wtn < SS_WT_ROWS) {
        st->play = 0u;                              /* stopped (or the wavetable not built yet) */
    } else if (!st->play) {                         /* start at bar 1 */
        st->play = 1u; st->bar = 0u; st->dbar = 0u; st->step = 0u; st->stepPh = 0.0f; st->fire = 1u;
        /* after a patch change with DrumBox: 2 count-in bars (its sounds
         * load; bass and keys wait), DrumBox plays them as hat quarters */
        st->cin = (st->cpend && st->dslot != ZM_NOSLOT) ? SS_COUNT_BARS : 0u;
        st->cpend = 0u;
        st->rseed = TSCL | 1u;                      /* Randm: new picks */
        st->pass = 0u;
        ss_swing(st, params);                       /* step 0 of the first drum bar */
        st->slen = 1.0f + st->swing;
    }
    if (!st->play) {
        for (j = 0u; j < 4u; ++j) st->vk[j].gate = 0.0f;
    }
    if (st->bar > st->pn) st->bar = 0u;              /* song got shorter (pn: rest to the drum bar's end) */
    ss_drums(st, params);
    inc = (float)(40u + zm_knob(params[5u + SS_K_TEMPO], SS_TEMPO_MAX)) * SS_STEP_PER_BPM;
    vol = (float)zm_knob(params[5u + SS_K_VOLUME], 100u) * 0.02f;
    j = ss_instr[zm_knob(params[5u + SS_K_INSTR], SS_NINSTR - 1u)];  /* Instr: keys part << 4 | bass part (0 = silent; drums always) */
    bg = (float)ss_bass_lvl[j & 15u] * (1.0f / SS_LVL_DIV);
    kg = (float)ss_keys_lvl[j >> 4] * (1.0f / SS_LVL_DIV);
    ki = ss_keys_id[j >> 4];                        /* instruments: record | table << 4 */
    bi = ss_bass_id[j & 15u];
    lm = 15u >> st->ms[SS_OPT_LEN];                 /* song bar line: step & lm == 0 */

    for (i = 0u; i < 8u; ++i) st->acc[i] = 0.0f;
    len = st->slen;                                 /* this 16th's length (swing) */
    i0 = 0u;                                        /* voices rendered up to here */
    i = 0u;                                         /* sequencer stepped up to here */
    do {
        ev = 0u;                                    /* at sample i: 1 events, 2 Once's stop */
        for (; i < 8u && st->play; ++i) {
            st->stepPh += inc;
            if (st->stepPh >= len) {
                st->stepPh -= len;
                st->fire = 1u;
                if (++st->step == 16u) st->step = 0u;
                if ((st->step & lm) == 0u) {    /* a song bar line (step 0: also a drum bar line) */
                    if (st->cin) {
                        if (st->step == 0u) st->cin--;      /* a count-in bar ended */
                    } else if (++st->bar >= st->pn) {
                        st->bar = st->pn;       /* ended inside a drum bar: rest up to its end */
                        if (st->step == 0u) {
                            st->bar = 0u;
                            st->pass++;             /* Randm picks again */
                            if (st->ms[SS_OPT_PLAY] == SS_PM_ONCE) {  /* Once: stop after the last bar */
                                st->done = 1u; st->play = 0u; st->fire = 0u;
                                ev = 2u;
                            }
                        }
                    }
                    if (st->step == 0u) {
                        st->dbar = st->bar;
                        ss_swing(st, params);
                    }
                }
                len = (st->step & st->swm) ? 1.0f - st->swing : 1.0f + st->swing;
                st->slen = len;
            }
            if (st->fire) {
                st->fire = 0u;
                if (!st->cin) ev = 1u;              /* count-in: silent */
            }
            if (ev) break;
        }
        e = ev ? i : 8u;                            /* the voices up to the event */
#ifdef SS_PROBE
        { uint32_t tv = TSCL;
#endif
        for (j = 0u; j < 4u; ++j)                   /* keys 0..2, then the bass */
            ss_run(&st->vk[j], &st->inst[(j < SS_VB ? ki : bi) & 15u], j < SS_VB ? kg : bg, inc,
                   &st->acc[i0], e - i0, SS_WT_OF(st) + ((j < SS_VB ? ki : bi) >> 4) * SS_WT_ROWS,
                   st->si, st->sf);
#ifdef SS_PROBE
        SS_PM_OF(st)->cur[3] += TSCL - tv; }
#endif
        if (ev == 1u) ss_events(st, params);
        if (ev == 2u)
            for (j = 0u; j < 4u; ++j) st->vk[j].gate = 0.0f;
        i0 = e;
        ++i;                                        /* sample e is stepped */
    } while (ev);
    for (i = 0u; i < 8u; ++i) {
        s = st->acc[i];
        if (s > 0.99f) s = 0.99f;
        if (s < -0.99f) s = -0.99f;
        s *= vol;
        fx[i] += s;
        fx[8u + i] += s;
    }
}

/* Byte n of a knob name (<= 7 characters + NUL), written only when it
 * differs. Returns n + 1. */
SS_COLD(ss_nput)
static uint32_t ss_nput(volatile unsigned char *nm, uint32_t n, uint32_t c)
{
    if (n < 8u && nm[n] != (unsigned char)c) nm[n] = (unsigned char)c;
    return n + 1u;
}

/* String i of the text pool (SS_S_*). */
SS_COLD(ss_str)
static const char *ss_str(const SsConst *k, uint32_t i)
{
    const char *p = k->strs;
    while (i)
        if (*p++ == 0) --i;
    return p;
}


void SS_AUDIO_FUNC(unsigned int *ctx)
{
    unsigned int *magicSrc = (unsigned int *)(uintptr_t)ctx[12];
    unsigned int *magicDst = (unsigned int *)(uintptr_t)(*(unsigned int *)(uintptr_t)ctx[11]);
    volatile unsigned int *desc;
    uintptr_t base, end;
    SsState *st;
    uint32_t t0, i, busy, ch, rh, idx;
#ifdef SS_PROBE
    SsMeter *pm;
    uint32_t tq;                        /* TSCL at the end of the last part */
#endif
    *magicDst = *magicSrc;
    t0 = TSCL;

    desc = (volatile unsigned int *)(uintptr_t)ctx[3];
    if (!desc) return;
    base = (uintptr_t)desc[0];
    end = (uintptr_t)desc[1];
    if (base == 0u || end <= base + sizeof(SsState) + 8u + SS_PM_BYTES + SS_WT_BYTES) return;
    /* state at the arena's end (a following delay reads the start first) */
    st = (SsState *)((end - sizeof(SsState)) & ~(uintptr_t)7u);
#ifdef SS_PROBE
    pm = SS_PM_OF(st);
    if (pm->magic != SS_PM_MAGIC || ((float *)(uintptr_t)ctx[1])[0] < 0.5f) {
        for (i = 0u; i < sizeof(SsMeter); ++i) ((volatile uint8_t *)pm)[i] = 0u;
        pm->magic = SS_PM_MAGIC;
    }
    i = t0 - st->lastCall;              /* the period (before lastCall is set) */
    if (i != 0u && i <= SS_AWAY_CYC) {
        pm->perSum += i;
        if (++pm->perCnt == 4096u) {
            pm->period = pm->perSum >> 12;
            pm->perSum = pm->perCnt = 0u;
        }
    }
#endif

    if (st->magic != SS_MAGIC || st->tail != SS_MAGIC) ss_fresh(st, ctx);
    st->k = &ss_c;
    if (st->n > SP_MAX_BARS || st->cn > SP_MAX_BARS || st->pn > st->n
        || ss_sum(st->bars, st->n) != st->sum || ss_sum(st->cand, st->cn) != st->csum) {
        /* overwritten while lent out: empty song, reload from Floppy */
        st->n = 0u; st->cn = 0u; st->pn = 0u;
        st->sum = 0u; st->csum = 0u;
        st->used = SP_KEY_BITS;
        st->packing = SS_PK_IDLE;
        st->vreq = 0u;
        st->zm.magic = 0u;
        st->wtn = 0u;
    }
    i = zm_s_poll(&st->zm, ctx);
    if (ss_second(st)) {                /* a second copy: passive; polled afresh */
        st->zm.magic = 0u;              /* (and the Floppy loaded) once it is the first */
        st->zm.posts = 0u;
        goto out;                       /* (one function end: a return costs code) */
    }
    ss_g = st;                          /* the active copy (knob texts) */
    if (i) {
        zm_s_get(&st->zm, st->w);
        st->lreq = 1u;
    }
#ifdef SS_PROBE
    tq = TSCL;
    pm->cur[0] = tq - t0;
#endif
    st->parts = st->zm.nparts ? st->zm.nparts : SP_MAX_PARTS;   /* without Floppy: measured as 4 */
    st->cap = st->zm.nparts ? SP_KEY_BITS + SP_BUDGET * st->parts : SS_NOFL_CAP;
    /* patch change (a gap between calls, or another patch number): from bar 1 */
    i = st->zm.ft ? ZM_U32(st->zm.ft->patch) : st->patch;
    if (t0 - st->lastCall > SS_AWAY_CYC || i != st->patch) {
        st->play = 0u;
        st->done = 0u;                  /* Once plays again */
        st->hold = SS_AWAY_HOLD;
        st->cpend = 1u;                 /* count in (if DrumBox is there) */
        st->wtn = 0u;                   /* the wavetable again (ctx[3] lent out) */
        /* MENU settings: PLAY LOOP; OCTAVE, LENGTH the Floppy's (a load
         * this call overrides them), defaults without Floppy */
        st->ms[SS_OPT_PLAY] = SS_PM_LOOP;
        ss_ms_set(st, st->zm.nparts ? st->fset : 0u);
    }
    st->patch = i;
    st->lastCall = t0;

    ss_edit(st, ctx);
    if (st->ereq) { st->ereq = 0u; ss_request_pack(st, SS_PK_EDIT); }

    if (st->sreq) {                     /* the one packer start */
        st->sreq = 0u;
        st->spn = ss_end(st->spsrc, st->spn);           /* songs: up to the first END */
        sp_start(&st->pk, st->spsrc, st->spn, st->sparts, st->spre);
    }
    busy = !(st->lreq | st->vreq) && st->packing;
    for (i = 0u; busy && i < SS_MAX_STEPS; ++i) {      /* the one packer step site */
        if (sp_step(&st->pk)) { ss_pack_done(st); busy = 0u; }
        if ((uint32_t)(TSCL - t0) >= SS_CYC) busy = 0u;
    }
    if (st->lreq | st->vreq) ss_unpack_job(st);   /* the one unpack site */

    if (st->jreq) {                     /* field knobs jump to the bar's values */
        st->jreq = 0u;
        ch = st->sel == 0u ? st->opt : 0u;
        rh = st->sel != 0u ? 0u : st->opt < SS_OPT_TRIM ? st->ms[st->opt] : st->last[SS_K_RHYTHM];
        if (st->sel > 0u) {
            idx = st->sel - 1u;
            if (idx < st->cn) {
                ch = st->cand[idx] & 31u;                   /* knob: chord + 1, Drums 25, Rest 26, END 0 */
                rh = SP_HAS_RHY(ch) ? (uint32_t)(st->cand[idx] >> 5) : ss_state_at(st->cand, idx);
                ch = ch < SP_REST ? ch + 1u : ch == SP_REST ? SS_CH_REST : ch == SP_DRUMS ? SS_CH_DRUMS : SS_CH_END;
            } else {
                rh = ss_state_at(st->cand, st->cn);
            }
        }
        zm_s_jump(&st->zm, ctx, SS_K_BAR, st->sel);    /* held at the last editable bar */
        zm_s_jump(&st->zm, ctx, SS_K_CHORD, ch);
        zm_s_jump(&st->zm, ctx, SS_K_RHYTHM, rh);
        st->last[SS_K_CHORD] = ch;
        st->last[SS_K_RHYTHM] = rh;
    }
    /* Page 1 knob maxes (descriptor +0x0C) follow the values each knob can
     * reach now: the OS draws a knob at value / max and stops a turned knob
     * at max. Bar MENU..STATUS (at least 2: max 1 draws an on/off switch),
     * Chord the MENU options or END..Rest, Rhythm NO / YES on TRIM, CLEAR
     * (max 1: a switch). The jumps above stay as the fallback. Rewritten
     * every call, like the names. */
    {
        volatile unsigned int *d = (volatile unsigned int *)ZM_DESC;
        uint32_t mb = ss_limit(st->n, st->used, st->cap, st->cn) + 1u;
        uint32_t mc = st->sel == 0u ? SS_NOPT - 1u : SS_CH_REST;
        uint32_t mr = st->sel == 0u && st->opt >= SS_OPT_TRIM ? 1u : 2u;
        if (mb < 2u) mb = 2u;
        if (!st->lreq && d[12u * (2u + SS_K_BAR) + 3u] != mb) d[12u * (2u + SS_K_BAR) + 3u] = mb;
        if (d[12u * (2u + SS_K_CHORD) + 3u] != mc) d[12u * (2u + SS_K_CHORD) + 3u] = mc;
        if (d[12u * (2u + SS_K_RHYTHM) + 3u] != mr) d[12u * (2u + SS_K_RHYTHM) + 3u] = mr;
    }
#ifdef SS_PROBE
    i = TSCL;
    pm->cur[1] = i - tq;
    tq = i;
#endif
    /* STATUS: the next message every ~1.5 s (TSCL >> 28): x - 3 * (x / 3),
     * x / 3 = x * 171 >> 9 (x < 256) */
    if (st->stat) {
        uint32_t v = t0 >> 28;
        st->blink = st->zm.nparts ? 2u : v - 3u * ((v * 171u) >> 9);
    }
    /* redraw when a text shown changes: the countdown digit, STATUS's names
     * and values (playing bar) */
    i = ss_clr_digit(st->clr);
    if (st->stat)
        i ^= 1u << 3 ^ st->blink << 4 ^ st->bar << 12 ^ st->used << 6 ^ st->packing << 15 ^ st->cap << 20 ^ st->zm.nparts << 25;
#ifdef SS_PROBE
    i ^= pm->shown[0] ^ pm->shown[1] << 11 ^ pm->shown[2] << 22;
#endif
    if (i != st->clrd) {
        st->clrd = i;
        if (st->zm.posts == 0u) { st->zm.posts = 1u; st->zm.postWait = 0u; }
    }
    /* page 1 names: Bar / Chord / Rhythm, on MENU the option's three words;
     * STATUS: built below. One name per call, in turn; all three again
     * every 16th call (the OS may restore them), at once when what they
     * show changes (the redraw key, Bar, the MENU option; the redraw then
     * waits until all three are written) and while a redraw is due. */
    i ^= st->sel << 7 ^ st->opt << 26 ^ st->stat << 31;
    if (i != st->nkey) {
        st->nkey = i;
        st->npend = 3u;
        if (st->zm.posts && st->zm.postWait < 2u) st->zm.postWait = 2u;
    }
    if (st->zm.posts || (++st->ntick & 15u) == 0u) st->npend = 3u;
    if (st->npend) {
        uint32_t k = st->nk > 2u ? 0u : st->nk;    /* (overwritten while lent out) */
        volatile unsigned char *nm = (volatile unsigned char *)&ZM_DESC[12u * (2u + SS_K_BAR + k)];
        st->nk = k + 1u;
        st->npend--;
        {               /* the name from the pool, the rest of the 8 bytes NUL */
                        /* STATUS: "Size:" "55/106" ("Pack" while packing), playing bar "B: 12"; without
                         * Floppy the first two in turn "Size:" "55/0", "Can't" "save!", "(no" "floppy)" */
            uint32_t v, v2, num, n, h, d, f;
            const char *p;
            {
                n = 0u; num = 0u; v2 = 0u; p = 0;
                if (!st->stat)
                    p = ss_str(st->k, SS_S_KNOB + (st->sel != 0u ? 0u : 3u + 3u * st->opt) + k);
                else
#ifdef SS_PROBE
                if (1) {                    /* load probe: "A" / "V" / "N" + peak % "/" mean % (the rest: dead) */
                    uint32_t m = k == 1u ? 3u : k;  /* meters 0, 3 (voices), 2 */
                    n = ss_nput(nm, n, k == 0u ? 'A' : k == 1u ? 'V' : 'N');
                    num = 2u; v = ss_pm_pct(pm, pm->pk[m]); v2 = ss_pm_pct(pm, pm->av[m]);
                    pm->shown[k] = (v << 10) + v2;
                } else
#endif
                if (k == 2u) {              /* "B: " + bar */
                    n = ss_nput(nm, n, 'B'); n = ss_nput(nm, n, ':'); n = ss_nput(nm, n, ' ');
                    num = 1u; v = st->bar + 1u;
                } else if (st->blink < 2u) {
                    p = ss_str(st->k, SS_S_WORD + SS_W_NOFL + 2u * st->blink + k);
                } else if (k == 0u) {
                    p = ss_str(st->k, SS_S_WORD + SS_W_SIZE);
                } else if (st->packing) {
                    p = ss_str(st->k, SS_S_WORD + SS_W_PACK);
                } else {                    /* used / capacity bits (0 without Floppy) */
                    num = 2u;
                    v = st->used < st->cap ? st->used : st->cap;
                    if (st->zm.nparts) v2 = st->cap;
                }
                if (p)
                    for (; *p; ++p) n = ss_nput(nm, n, (uint32_t)*p);
                while (num) {               /* v < 3000; num 2: then "/" v2 */
                    f = 0u;                 /* a digit written: zeros count from here */
                    for (d = 1000u; d > 1u; d = d == 1000u ? 100u : d == 100u ? 10u : 1u) {
                        h = 0u;
                        while (v >= d) { v -= d; h++; }
                        if (h | f) { n = ss_nput(nm, n, '0' + h); f = 1u; }
                    }
                    n = ss_nput(nm, n, '0' + v);
                    if (--num) n = ss_nput(nm, n, '/');
                    v = v2;
                }
                while (n < 8u) n = ss_nput(nm, n, 0u);
            }
        }
    }
    if (st->full) st->full--;
#ifdef SS_PROBE
    pm->cur[2] = TSCL - tq;
#endif
    if (st->wtn > SS_WT_ROWS) st->wtn = 0u;            /* (overwritten while lent out) */
    if (st->wtn < SS_WT_ROWS) {
        i = SS_WT_ROWS - st->wtn;
        if (i > SS_WT_STEP) i = SS_WT_STEP;
        ss_wt_build(st->k, SS_WT_OF(st), st->wtn, i, (uint32_t *)st->inst);
        st->wtn += i;
    }
#ifdef SS_PROBE
    ss_play(st, ctx);
    ss_pm_end(pm);
#else
    ss_play(st, ctx);
#endif
out:
    zm_s_post(&st->zm);                 /* last: the OS call ends the function */
}

/* ------------------------------------------------------------- knob texts */

/* Up to m characters of p into s (NUL-terminated); returns the length. */
ZM_INLINE(ss_cpy)
static inline int ss_cpy(char *s, const char *p, int m)
{
    int i;
    for (i = 0; i < m && p[i]; ++i) s[i] = p[i];
    s[i] = 0;
    return i;
}

/* Knob k (0-based), raw value v. One shared copy in .text (called by the
 * GetString_Ss* wrappers, PC-relative, no relocation). A text = s[0..n)
 * prefix + text pool string w (+ "m" for a minor chord), copied once at the
 * end; numbers go through the one digit routine: s[0..n), then v (0..299). */
ZM_EXPAND_PRAGMA(CODE_SECTION(ss_text, ".text"))
ZM_EXPAND_PRAGMA(FUNC_CANNOT_INLINE(ss_text))
static int ss_text(uint32_t k, uint32_t v, char *s)
{
    const volatile SsState *st = ss_g;
    uint32_t sel = 0u, stat = 0u, idx, h = 0u, t = 0u, w = ~0u;  /* w: text pool string (~0: a number) */
    uint32_t w2 = ~0u, skip = 0u;       /* Instr: then string w2 from its character skip */
    int n = 0;
    if (st) { sel = st->sel; stat = st->stat; }
    idx = sel - 1u;
    if (k - SS_K_RHY1 < 3u) {           /* Rhy1..3: "Randm", pattern names (6 bits a character) */
        h = ss_pat_names[v < SS_NPAT ? v : SS_NPAT];
        for (; n < 5 && (t = h & 63u) != 0u; ++n, h >>= 6)
            s[n] = (char)(t + (t < 11u ? '0' - 1u : t < 37u ? 'A' - 11u : 'a' - 37u));
        s[n] = 0;
        return n;
    } else if (k == SS_K_INSTR) {       /* the kit's text from its parts (sets.py kit_name) */
        h = st->k->instr[v < SS_NINSTR ? v : SS_NINSTR - 1u];
        t = h & 15u;                    /* bass part */
        h >>= 4;                        /* chords part */
        w = SS_S_INSTR + 1u + h - (h + t == 0u);    /* chords part name, "" (none), "Mute" */
        /* then "+" + the bass part name (alone: the name, "B1"; no bass:
         * "Ch"), unless the chords part plays its own bass (same id); the
         * silent parts share an id no part has (sets.py): none for "Mute" */
        if (st->k->keys_id[h] != st->k->bass_id[t])
            w2 = SS_S_INSTR + SS_NKEYI + 1u + t, skip = h == 0u;
    } else if (k == SS_K_TEMPO) {       /* BPM */
        v = 40u + (v > SS_TEMPO_MAX ? SS_TEMPO_MAX : v);
    } else if (!st || k == SS_K_VOLUME) {
        /* plain number */
    } else if (k == SS_K_BAR) {
        if (v == 0u) w = SS_S_WORD + SS_W_MENU;
        else if (v > ss_limit(st->n, st->used, st->cap, st->cn)) w = SS_S_WORD + SS_W_STATUS;
    } else if (stat) {      /* STATUS: playing chord "Am", arrow + the next chord ("---": no song) */
        idx = st->bar;
        if (idx >= st->pn) {
            w = SS_S_WORD + SS_W_DASH3;
        } else {
            if (k == SS_K_RHYTHM) idx = idx + 1u < st->pn ? idx + 1u : 0u;    /* next bar (wraps) */
            h = st->bars[idx] & 31u;    /* 0..23 root * 2 + minor, "Rest", "Drums" */
            if (h == SS_B_END) h = SP_REST;
            if (k == SS_K_RHYTHM) {     /* glyph 0x1C (arrow), 2 spaces; 1 before "Rest" / "Drums" (7 don't fit) */
                s[0] = 0x1C; s[1] = ' '; n = 2;
                if (h < 24u) s[n++] = ' ';
            }
            w = SS_S_CHORD;
        }
    } else if (k == SS_K_CHORD) {
        if (sel == 0u)                  /* MENU options */
            w = SS_S_WORD + (v < SS_NOPT ? v : SS_NOPT - 1u);
        else if (st->full)              /* refused: "Full", "Err", "Flop?" (a Floppy missing) */
            w = SS_S_WORD + (st->err == 2u ? SS_W_FLOP : st->err ? SS_W_ERR : SS_W_FULL);
        else if (v == SS_CH_END) w = SS_S_WORD + SS_W_END;
        else {
            h = v == SS_CH_DRUMS ? SP_DRUMS : v >= SS_CH_REST ? SP_REST : v - 1u;
            w = SS_S_CHORD;
        }
    } else if (k == SS_K_RHYTHM) {
        if (sel == 0u) {
            h = st->opt;
            if (h < SS_OPT_TRIM)        /* OFF / ONCE / LOOP, -1 / 0 / +1, BAR / HALF / BEAT */
                w = SS_S_WORD + SS_W_OFF + 3u * h + (v > 2u ? 2u : v);
            else                        /* TRIM, CLEAR: NO / YES / YES 5 .. YES 1, DONE */
                w = SS_S_WORD + (v ? SS_W_YES : st->cdone ? SS_W_DONE : SS_W_NO);
        } else if (idx >= st->cn || !SP_HAS_RHY(st->cand[idx] & 31u)) {
            w = SS_S_WORD + SS_W_DASH3; /* no rhythm: past the song end, a rest or END */
        } else {
            s[0] = 'R'; s[1] = 'h'; s[2] = 'y'; n = 3;      /* "Rhy1".."Rhy3" */
            v = (v > 2u ? 2u : v) + 1u;
        }
    }
    if (w != ~0u) {                     /* the one string copy */
        t = 0u;
        if (w == SS_S_CHORD) {          /* bar code h: root * 2 + minor, Rest 24, Drums 26 */
            w += h >> 1;                /* Rest 12, Drums 13 */
            t = h < 24u && (h & 1u);
        }
        n += ss_cpy(s + n, ss_str(st->k, w), 7 - n);
        if (w2 != ~0u) {                /* Instr: + string w2 from its character skip */
            n += ss_cpy(s + n, ss_str(st->k, w2) + skip, 7 - n);
        }
        if (t) { s[n++] = 'm'; s[n] = 0; }
        if (w == SS_S_WORD + SS_W_YES && st->clr) {
            s[3] = ' '; s[4] = (char)('0' + ss_clr_digit(st->clr)); s[5] = 0;
            n = 5;
        }
        return n;
    }
    if (v >= 200u) { v -= 200u; h = 2u; }
    if (v >= 100u) { v -= 100u; h = 1u; }
    if (v >= 80u) { v -= 80u; t += 8u; }
    if (v >= 40u) { v -= 40u; t += 4u; }
    if (v >= 20u) { v -= 20u; t += 2u; }
    if (v >= 10u) { v -= 10u; t += 1u; }
    if (h) s[n++] = (char)('0' + h);
    if (h || t) s[n++] = (char)('0' + t);
    s[n++] = (char)('0' + v);
    s[n] = 0;
    return n;
}

ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss1, ".text"))
int GetString_Ss1(unsigned int v, char *s) { return ss_text(0u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss2, ".text"))
int GetString_Ss2(unsigned int v, char *s) { return ss_text(1u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss3, ".text"))
int GetString_Ss3(unsigned int v, char *s) { return ss_text(2u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss4, ".text"))
int GetString_Ss4(unsigned int v, char *s) { return ss_text(3u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss5, ".text"))
int GetString_Ss5(unsigned int v, char *s) { return ss_text(4u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss6, ".text"))
int GetString_Ss6(unsigned int v, char *s) { return ss_text(5u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss7, ".text"))
int GetString_Ss7(unsigned int v, char *s) { return ss_text(6u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss8, ".text"))
int GetString_Ss8(unsigned int v, char *s) { return ss_text(7u, v, s); }
ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Ss9, ".text"))
int GetString_Ss9(unsigned int v, char *s) { return ss_text(8u, v, s); }
