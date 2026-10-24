/*
 * DrumBox -- synthesized drum machine for Zoom MS-70CDR (fw 2.10).
 *
 * Drums come from a palette (drums/, one file per drum, kits.py) of two
 * kinds; a kit can mix them. Synth drums have their own code
 * (drums/<algo>.c, included through drumbox_synth.h). Sampled drums are
 * data for one generic engine (analysis-resynthesis, models made from WAV
 * samples by drum_analyze.py, tables in drumbox_kits.h):
 *   partials  up to 4 sines per drum, each with a pitch glide and an
 *             amplitude envelope
 *   bands     noise through a fixed bank of 25 third-octave band-pass
 *             filters shared by all drums; per drum an amplitude envelope
 *             per band
 * Envelopes are one code per point of a fixed time grid, linear between
 * points (the resynthesis engine). A kit maps the eight lanes (kick,
 * snare, closed hat, open hat, ride, crash, high tom, low tom) to drums; a
 * closed hat chokes the open-hat lane.
 *
 * Pre-rendered kit (the live engine took ~60 % of the G1Xon's DSP, so
 * chains crackled): when the kit changes, each lane's drum is
 * rendered once at velocity 1 by the engine into an int16 cache in ctx[3]
 * after the state, in the background (DB_RENDER_CYC cycles per call),
 * its quiet tail trimmed;
 * hits play the cache x velocity. A lane plays once it is rendered; the
 * closed hat renders first and counts quarters (count-in) until the rest
 * is ready, then the song starts at the next bar line (after 1 or an even
 * number of count bars).
 *
 * Sequencer: a song of two sections of 16ths, played in turn: BeatA for
 * LenA bars with FillA laid over the end of the last bar, then BeatB for
 * LenB bars with FillB (1 to 64 bars each, 2/4/8/16/32/64 also with the
 * fill in the middle, or Skip: the section is left out and the other one
 * loops); crash fills add a
 * crash on the next downbeat (drumbox_patterns.h, generated from
 * patterns.py), read from const tables in .const (their addresses get
 * .rela.dyn entries).
 * Footswitch ON starts at section A beat 1; OFF stops. Guitar passes through.
 *
 * Pedal-safety rules followed (repeat98 docs/SAFE-DSP-RULES.md):
 *   - all state in ctx[3] arena, no .bss/.fardata, no stack arrays
 *   - no libm, no double, no integer divide/modulo, no float divide
 *   - every helper force-inlined so .audio is the only code section
 *   - ctx[11]/ctx[12] magic shuttle preserved
 */

#include <stdint.h>

#include "drumbox_params.h"
#include "drumbox_patterns.h"
#include "drumbox_kits.h"

#ifndef DB_AUDIO_FUNC
#define DB_AUDIO_FUNC Fx_SFX_DrumBox
#endif

#define DB_DO_PRAGMA(x) _Pragma(#x)
#define DB_EXPAND_PRAGMA(x) DB_DO_PRAGMA(x)
#define DB_CODE_SECTION(func) DB_EXPAND_PRAGMA(CODE_SECTION(func, ".audio"))
#define DB_ALWAYS_INLINE(func) DB_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(func))
DB_CODE_SECTION(DB_AUDIO_FUNC)
/* Loops (band bank, envelopes) must stay interruptible: no DINT/RINT. */
DB_EXPAND_PRAGMA(FUNC_INTERRUPT_THRESHOLD(DB_AUDIO_FUNC, 1))
DB_EXPAND_PRAGMA(CODE_SECTION(GetString_DbTempo, ".text"))
DB_EXPAND_PRAGMA(CODE_SECTION(GetString_DbBeat, ".text"))
DB_EXPAND_PRAGMA(CODE_SECTION(GetString_DbFill, ".text"))
DB_EXPAND_PRAGMA(CODE_SECTION(GetString_DbKit, ".text"))
DB_EXPAND_PRAGMA(CODE_SECTION(GetString_DbLength, ".text"))

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

#define DB_MAGIC   0x44524D42u   /* 'DRMB' */
#define DB_VERSION 41u  /* 41: countN; 40: rMask, closed hat rendered first; 39: start gate (lastCall, ckWait); 38: cache check; 37: synp, synth params of the drum being rendered (bump when the state layout changes) */
/* A crash fill crashes the next downbeat only if the last crash is at
 * least this many bars back (short sections: a crash every 4th bar at
 * most). Must match CRASH_GAP in preview.py. */
#define DB_CRASH_GAP 4
/* Kit cache: each call renders until it has used DB_RENDER_CYC TSCL
 * cycles (~25 % of a call at 180 MHz; the 16-sample render took ~75 % and
 * crackled on a kit change), at most DB_RENDER_MAX samples (TSCL stopped:
 * always that many); a drum ends after DB_QUIET samples under DB_LOUD
 * (cache units, 1/16384 each). */
#define DB_RENDER_CYC 8000u
#define DB_RENDER_MAX 32
#define DB_QUIET    4410
#define DB_LOUD     2.0f
#define DB_CACHE_ONE 16384.0f
/* Lane rendered r-th, one nibble each: closed hat (lane 2) first for the
 * count-in, then kick, snare, open hat .. low tom (a nibble list is
 * smaller code than compares). Mirror: RENDER_ORDER in preview.py. */
#define DB_RENDER_LANE(r) ((int)((0x76543102u >> ((r) << 2)) & 15u))
/* Cache check: the pedal gives a slot's memory to other patches' effects
 * and back with our state intact, so a ready kit's cache may hold their
 * data (noise on the hits). Once the kit is ready, each
 * call sums DB_CHECK_N more cache samples; the first full pass stores the
 * sum, every later pass must match it, else the kit renders again
 * (Synth ~0.3 s per pass). */
#define DB_CHECK_N  256
/* Start gate (else the first notes are silent after adding DrumBox or
 * loading a patch with it): the song starts only once the
 * kit is rendered (also after a Kit knob change); while the other lanes
 * render, the closed hat counts quarters, beat 1 accented. A gap of more than DB_AWAY_CYC TSCL cycles between
 * calls (~50 ms at 180 MHz; calls come every ~33,000) means the pedal ran
 * other effects in the slot (patch switch): the song stops and waits for
 * a full cache check pass at DB_CHECK_FAST samples per call (~15 ms for
 * the biggest kit), which renders the kit again if it was overwritten; a
 * kit not fully rendered yet renders again from the start. */
#define DB_AWAY_CYC 9000000u
#define DB_CHECK_FAST 4096
/* Unaccented hits (fixed accent depth) */
#define DB_VEL_LO 0.72f

#define DB_INV_SR      (1.0f / 44100.0f)
/* Synth drums' noise seeds: lane i starts at SEED + i * STEP (preview.py) */
#define DB_SYN_RNG_SEED 0x1234567u
#define DB_SYN_RNG_STEP 0x9E3779B9u
/* 16th notes per sample per BPM: 4 / (60 * 44100) */
#define DB_STEP_PER_BPM 1.5117158e-6f

/* A lane playing a synth drum: its own noise source, the choke fade and
 * the drum's state words (a struct of its own, see drums/<algo>.c). */
typedef struct DBSyn {
    int32_t  active;
    uint32_t rng;
    float    fade, fadeStep;    /* output gain; fadeStep < 0 while choked */
    float    s[DB_SYN_WORDS];
} DBSyn;
/* A synth drum's state struct must fit DBSyn.s (STATE in its .py file). */
#define DB_SYN_FITS(T) typedef char T##_fits[(sizeof(T) <= sizeof(((DBSyn *)0)->s)) ? 1 : -1]

/* One pattern lane: the drum the kit puts there, playing. */
typedef struct DBLane {
    int32_t active, t, seg;     /* resynthesis drum playing; t: samples since the hit; seg: grid segment */
    int32_t drum;               /* the drum it was hit with (a kit change mid-ring keeps it) */
    int32_t kind;               /* its kind: 0 resynthesis, k synth algorithm k */
    DBSyn   syn;                /* synth drum state (kind > 0) */
    float   vel;
    float   bcur[DB_NUM_BANDS], bslope[DB_NUM_BANDS];      /* band envelopes */
    float   pcur[DB_MAX_PARTIALS], pslope[DB_MAX_PARTIALS]; /* partial envelopes */
    float   ph[DB_MAX_PARTIALS], pd[DB_MAX_PARTIALS];       /* phase, glide offset (Hz) */
    /* compressed rows: byte offset in E->env and the code of the point the
     * envelope is heading to */
    int32_t boff[DB_NUM_BANDS], bcode[DB_NUM_BANDS];
    int32_t poff[DB_MAX_PARTIALS], pcode[DB_MAX_PARTIALS];
    /* real-sample start (drum_sample.py): sample + 1 playing (0 none),
     * samples since the hit, 44.1 kHz step 0..DB_SMP_DIV - 1 between the
     * decoded samples yA, yB, next code's bit and the DPCM step size */
    int32_t smp, sPos, sSub, sBit;
    float   sYA, sYB, sStep;
} DBLane;

typedef struct DBState {
    uint32_t magic;
    uint32_t version;
    uint32_t running;
    int32_t  step;          /* current 16th of the bar, 0..15 */
    int32_t  sect;          /* song section being played, 0 or 1 */
    int32_t  bar;           /* bar within the section, 0..its length - 1 */
    float    phase;         /* progress through current step, in 16th units */
    /* the bar's swing (from its beat, DB_SWING_*): a step with
     * (step & swMask) != 0 lasts 1 - swing, the others 1 + swing */
    float    swing;
    uint32_t swMask;

    /* two noise sources (even / odd bands), their last two samples, and
     * the band-pass bank's state; g: summed band gains of this sample */
    const DBEng *eng;       /* &db_eng, stored each call (see DB_AUDIO_FUNC) */
    uint32_t rngA, rngB;
    float xa1, xa2, xb1, xb2;
    float y1[DB_NUM_BANDS], y2[DB_NUM_BANDS], g[DB_NUM_BANDS];
    DBLane lane[DB_NUM_LANES];
    /* the bar being played: beat with its fill laid over (db_compose_bar) */
    uint32_t cur[DB_BEAT_WORDS];
    /* Randm fill picker (own xorshift32), the end fill played in the
     * previous bar (its crash flag, see db_compose_bar; 0 after a middle
     * fill) and the bars since the last downbeat crash */
    uint32_t fillRng;
    int32_t  lastFill;
    int32_t  sinceCrash;
    /* Randm beat: own xorshift32 and the beat it picked (-1: none yet,
     * pick at the next bar line) */
    uint32_t beatRng;
    int32_t  randBeat;
    /* the cache: kit in it (-1: none), lanes done in render order
     * (DB_RENDER_LANE: closed hat first), samples of the one being
     * rendered, its last loud sample, cache samples used before it; per
     * lane its start and length; rMask: ready lanes (bit per lane) */
    int32_t  cKit, rLane, rPos, rLast, rEnd;
    uint32_t rMask;
    int32_t  countN;            /* count-in bars played - 1 (-1: none; smaller test code) */
    int32_t  rBLo, rBHi;        /* bands of the drum being rendered (none for synth drums) */
    float    synp[DB_SYN_NP];   /* params of the synth drum being rendered (from E->syn_pal) */
    int32_t  cOff[DB_NUM_LANES], cLen[DB_NUM_LANES];
    /* cache check: sum of the ready cache (ckOk: ckRef holds it), the
     * running sum and its position */
    int32_t  ckOk, ckPos;
    uint32_t ckRef, ckAcc;
    /* start gate: TSCL at the previous call; ckWait: the sequencer waits
     * for a full check pass (set after a gap between calls) */
    uint32_t lastCall;
    int32_t  ckWait;
    /* players, two per lane (vCur: the newer one; the other fades out):
     * cache index, samples left, gain, gain step (< 0: fading out) */
    int32_t  vCur[DB_NUM_LANES];
    int32_t  vIdx[2 * DB_NUM_LANES], vRem[2 * DB_NUM_LANES];
    float    vGain[2 * DB_NUM_LANES], vStep[2 * DB_NUM_LANES];
} DBState;

DB_ALWAYS_INLINE(db_align4)
static inline uintptr_t db_align4(uintptr_t x)
{
    return (x + 3u) & ~(uintptr_t)3u;
}

DB_ALWAYS_INLINE(db_clamp)
static inline float db_clamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

DB_ALWAYS_INLINE(db_abs)
static inline float db_abs(float x)
{
    return (x < 0.0f) ? -x : x;
}

/* Knob raw value -> 0..1. The synthesized LineSel handlers write the knob
 * value / 100 whatever the knob's max (knob 6 of a max-6 knob = 0.06), as
 * themanro's zoom_param_norm01 says (confirmed on the G1Xon).
 * The _init (ctx[34] + every knob's edit handler, see build/linker.py)
 * writes the screen's values into params[] on add, patch load and power-up,
 * so they are read as they are; NaN / negative words fall back to the
 * default. */
DB_ALWAYS_INLINE(db_param)
static inline float db_param(float raw, float fallback)
{
    if (raw != raw || raw < 0.0f) return fallback;
    return db_clamp(raw, 0.0f, 1.0f);
}

/* The same for the list knobs (Beat, MidFill, Fill): max up to 150, so
 * raw = v / 100 runs to 1.5 (LineSel's handler law stops there). */
DB_ALWAYS_INLINE(db_param_list)
static inline float db_param_list(float raw, float fallback)
{
    if (raw != raw || raw < 0.0f) return fallback;
    return db_clamp(raw, 0.0f, 1.5f);
}

/* One cycle per unit phase; parabolic sine with one refinement pass
 * (~0.1% error, inverted sign -- irrelevant for audio). */
DB_ALWAYS_INLINE(db_sin)
static inline float db_sin(float p)
{
    float x = p * 2.0f - 1.0f;
    float y = 4.0f * x * (1.0f - db_abs(x));
    return y + 0.225f * (y * db_abs(y) - y);
}

DB_ALWAYS_INLINE(db_wrap)
static inline float db_wrap(float p)
{
    return (p >= 1.0f) ? p - 1.0f : p;
}

DB_ALWAYS_INLINE(db_xorshift)
static inline float db_xorshift(uint32_t *r)
{
    uint32_t x = *r;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return (float)(int32_t)x * 4.6566129e-10f;
}

/* Helpers for the synth drums (drums/<algo>.c); Python mirrors in synth_lib.py. */
DB_ALWAYS_INLINE(db_frac)
static inline float db_frac(float p)
{
    float f = p - (float)(int)p;
    return (f < 0.0f) ? f + 1.0f : f;
}

/* Naive square wave, +-1. */
DB_ALWAYS_INLINE(db_sq)
static inline float db_sq(float p)
{
    return (p < 0.5f) ? 1.0f : -1.0f;
}

DB_ALWAYS_INLINE(db_lerp)
static inline float db_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Gentle cubic soft clip, unity slope at 0, saturates at +-1. */
DB_ALWAYS_INLINE(db_soft_clip)
static inline float db_soft_clip(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - 0.14814815f * x * x * x;
}

/* The lane's own white noise. */
DB_ALWAYS_INLINE(db_noise)
static inline float db_noise(DBSyn *v)
{
    return db_xorshift(&v->rng);
}

/* Six detuned squares (metallic hat tone): phase steps per sample. Loops
 * over them stay rolled (UNROLL(1)): ~1 KB less code per drum using them. */
static const float db_metal_inc[6] = {
    205.3f * DB_INV_SR, 304.4f * DB_INV_SR, 369.6f * DB_INV_SR,
    522.7f * DB_INV_SR, 540.0f * DB_INV_SR, 800.0f * DB_INV_SR,
};

/* Advance the metal phases hm[0..5] and return the mean of the squares at
 * mult x their phase (mult 1 or 2: an octave up). */
DB_ALWAYS_INLINE(db_metal_sq)
static inline float db_metal_sq(float *hm, float mult)
{
    float acc = 0.0f;
    int j;
#pragma UNROLL(1)
    for (j = 0; j < 6; j++) {
        float h = db_wrap(hm[j] + db_metal_inc[j]);
        hm[j] = h;
        acc += db_sq(db_wrap(h * mult));
    }
    return acc * 0.16666667f;
}

/* The synth drums' code (drums/<algo>.c) and DB_SYN_HIT / DB_SYN_RENDER. */
#include "drumbox_synth.h"

/* The engine as new before lane first's render (renders don't depend on
 * what was rendered before): noise, bank, lanes first..7 (the lanes
 * before it were stopped when their render ended). Mirror: a new Voice in
 * preview.py. */
DB_ALWAYS_INLINE(db_reset_engine)
static inline void db_reset_engine(DBState *st, int first)
{
    volatile float *f;
    int i;
    st->rngA = 0x1234567u;
    st->rngB = 0x2C9277B5u;
    st->xa1 = st->xa2 = st->xb1 = st->xb2 = 0.0f;
    /* y1, y2, g and the lanes are all float / int32 zeros: clear them as
     * words (volatile: no unrolled uninterruptible store block) */
    f = (volatile float *)st->y1;
    for (i = 0; i < 3 * DB_NUM_BANDS; i++)
        f[i] = 0.0f;
    f = (volatile float *)&st->lane[first];
    for (i = 0; i < (DB_NUM_LANES - first) * (int)(sizeof(DBLane) / sizeof(float)); i++)
        f[i] = 0.0f;
    for (i = first; i < DB_NUM_LANES; i++)  /* synth drums' noise, one source per lane */
        st->lane[i].syn.rng = DB_SYN_RNG_SEED + (uint32_t)i * DB_SYN_RNG_STEP;
}

DB_ALWAYS_INLINE(db_init_state)
static inline void db_init_state(DBState *st)
{
    st->magic = DB_MAGIC;
    st->version = DB_VERSION;
    st->running = 0u;
    st->step = 15;
    st->sect = 1;
    st->bar = 15;
    st->phase = 0.0f;
    st->swing = 0.0f;
    st->swMask = 1u;
    st->cKit = -1;              /* the first call renders the kit (and stops the players) */
    st->cur[0] = st->cur[1] = st->cur[2] = st->cur[3] = st->cur[4] = 0u;
    st->fillRng = DB_FILL_RNG_SEED;
    st->lastFill = 0;
    st->sinceCrash = DB_CRASH_GAP;
    st->beatRng = DB_BEAT_RNG_SEED;
    st->randBeat = -1;
}

/* Effective bar = beat with the fill laid over its end: lanes the fill
 * replaces are cleared from the window start w to step 15, then the fill's
 * hits are ORed in. If the previous bar's fill is a crash fill (header
 * bit 4), beat 1 gets a crash. Runs once per bar. Must match compose() in
 * patterns.py. */
DB_ALWAYS_INLINE(db_clear_mask)
static inline uint32_t db_clear_mask(uint32_t m, uint32_t win, uint32_t winHi)
{
    return ((0u - (m & 1u)) & win) | ((0u - ((m >> 1) & 1u)) & winHi);
}

DB_ALWAYS_INLINE(db_compose_bar)
static inline void db_compose_bar(DBState *st, int beat, int fill, int prevFill)
{
    int b = beat * DB_BEAT_WORDS;
    int f = fill * DB_FILL_WORDS;
    uint32_t h = DB_FILL_WORD(f);
    uint32_t m = h >> 8;
    uint32_t win = (0xFFFFu << (h & 15u)) & 0xFFFFu;
    uint32_t winHi = win << 16;
    uint32_t crash = (DB_FILL_WORD(prevFill * DB_FILL_WORDS) & 16u) << 12;   /* bit 4 -> bit 16 */
    st->cur[0] = (DB_BEAT_WORD(b) & ~db_clear_mask(m, win, winHi)) | DB_FILL_WORD(f + 1);
    st->cur[1] = (DB_BEAT_WORD(b + 1) & ~db_clear_mask(m >> 2, win, winHi)) | DB_FILL_WORD(f + 2);
    st->cur[2] = (DB_BEAT_WORD(b + 2) & ~db_clear_mask(m >> 4, win, winHi)) | DB_FILL_WORD(f + 3) | crash;
    st->cur[3] = (DB_BEAT_WORD(b + 3) & ~db_clear_mask(m >> 6, win, winHi)) | DB_FILL_WORD(f + 4);
    st->cur[4] = (DB_BEAT_WORD(b + 4) & ~db_clear_mask(m >> 8, win, winHi)) | DB_FILL_WORD(f + 5);
}

/* Fill of bar `bar` of a section `bars` long: its fill on the last bar
 * (and with mid on the first half's last bar), else none. Must match
 * section_fill() in patterns.py. */
DB_ALWAYS_INLINE(db_section_fill)
static inline int db_section_fill(int bar, int bars, int mid, int fill)
{
    return (bar == bars - 1 || (mid && bar == (bars >> 1) - 1)) ? fill : 0;
}

/* Knob fill -> fill to play: Randm picks one of the fills
 * 2..2+DB_FILL_RANDOM_COUNT-1 each time (all but the stops / breaks at the
 * end; integer only). Must match resolve_fill() in patterns.py. */
DB_ALWAYS_INLINE(db_resolve_fill)
static inline int db_resolve_fill(DBState *st, int fill)
{
    uint32_t r;
    if (fill != DB_FILL_RANDOM) return fill;
    r = st->fillRng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    st->fillRng = r;
    return 2 + (int)(((r >> 16) * (uint32_t)DB_FILL_RANDOM_COUNT) >> 16);
}

/* Randm beat: one of all the real beats 2..N-1 (integer only). Must match
 * resolve_beat() in patterns.py. */
DB_ALWAYS_INLINE(db_pick_beat)
static inline int db_pick_beat(DBState *st)
{
    uint32_t r = st->beatRng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    st->beatRng = r;
    return DB_BEAT_RANDOM_FIRST + (int)(((r >> 16) * (uint32_t)DB_BEAT_RANDOM_COUNT) >> 16);
}

/* Envelope code q (0..127) -> 2^(-(127 - q) / 12), 0 for q = 0; the
 * integer split and the exponent bits avoid pow (kits.amp_of_code). */
DB_ALWAYS_INLINE(db_amp_code)
static inline float db_amp_code(const DBEng *E, uint32_t q)
{
    union { uint32_t u; float f; } p2;
    uint32_t e, k;
    if (q == 0u) return 0.0f;
    e = 127u - q;
    k = (e * 2731u) >> 15;              /* e / 12 for e <= 127 */
    p2.u = (127u - k) << 23;            /* 2^-k */
    return E->frac12[e - 12u * k] * p2.f;
}

/* Drum d header fields (kits.py write_header); for a synth drum BOFF is
 * its first param in E->syn_idx */
#define DB_D_LO(d)    ((int)E->drum_hdr[(d) * 7])
#define DB_D_HI(d)    ((int)E->drum_hdr[(d) * 7 + 1])
#define DB_D_NP(d)    ((int)E->drum_hdr[(d) * 7 + 2])
#define DB_D_BOFF(d)  ((int)E->drum_hdr[(d) * 7 + 3])
#define DB_D_POFF(d)  ((int)E->drum_hdr[(d) * 7 + 4])
#define DB_D_PART(d)  ((int)E->drum_hdr[(d) * 7 + 5])
#define DB_D_KIND(d)  ((int)E->drum_hdr[(d) * 7 + 6])

/* Compressed envelope row at off (kits.encode_row): byte 0 = L points |
 * S / 2 << 5 (S even), byte 1 = code of point S, then a 4-bit step per
 * point after S; points before S (leading silence) are 0. */
#define DB_ROW_L(x0)      ((x0) & 31)
#define DB_ROW_S(x0)      (((x0) >> 4) & 14)
#define DB_ROW_BYTES(x0)  (DB_ROW_L(x0) ? 2 + ((DB_ROW_L(x0) - DB_ROW_S(x0)) >> 1) : 1)

/* Code of point j of the row at off, given point j - 1's code (j = 0:
 * the row's first code). */
DB_ALWAYS_INLINE(db_code_next)
static inline int32_t db_code_next(const DBEng *E, int off, int j, int32_t code)
{
    int x0 = E->env[off];
    int S = DB_ROW_S(x0), nib;
    if (j >= DB_ROW_L(x0) || j < S) return 0;
    if (j == S) return E->env[off + 1];
    j -= S + 1;
    nib = E->env[off + 2 + (j >> 1)];
    nib = (j & 1) ? (nib >> 4) : (nib & 15);
    code += E->dt[nib];
    return (code < 0) ? 0 : ((code > 127) ? 127 : code);
}

/* Next decoded sample of lane s's sample si: pred = a * last, code =
 * magnitude k | sign << (bits - 1) from an LSB-first bit stream, step
 * adapted by multiplier k (kits.SMP_MULT). Mirror: preview.smp_next. */
DB_ALWAYS_INLINE(db_smp_next)
static inline float db_smp_next(const DBEng *E, DBLane *s, int si)
{
    const float *f = &E->smp_f[si * 8];
    int bits = E->smp_hdr[si * 4 + 1];
    int p = s->sBit;
    uint32_t w = (uint32_t)E->smp_data[p >> 3] | ((uint32_t)E->smp_data[(p >> 3) + 1] << 8);
    uint32_t half = 1u << (bits - 1);
    uint32_t c = (w >> (p & 7)) & (2u * half - 1u);
    int k = (int)(c & (half - 1u));
    float q = (float)k + 0.5f;
    float y = f[0] * s->sYB + ((c & half) ? -q : q) * s->sStep;
    s->sBit = p + bits;
    s->sStep = db_clamp(s->sStep * f[4 + k], DB_SMP_STEP_MIN, DB_SMP_STEP_MAX);
    return y;
}

/* Start drum d on lane s, which db_reset_engine has just cleared (renders
 * only): a synth drum by its own hit code, a resynthesis drum with its
 * envelopes heading for point 0 of each row. Mirror: Voice.hit in
 * preview.py. */
DB_ALWAYS_INLINE(db_hit)
static inline void db_hit(const DBEng *E, DBLane *s, int d, float vel, const float *sp)
{
    int b, i, lo = DB_D_LO(d), hi = DB_D_HI(d), np = DB_D_NP(d);
    int off = DB_D_BOFF(d);
    int kind = DB_D_KIND(d);
    float bv = E->drum_scale[2 * d] * vel;
    float pv = E->drum_scale[2 * d + 1] * vel;
    s->drum = d;
    s->kind = kind;
    if (kind) {
        s->syn.active = 1;
        DB_SYN_HIT(kind, &s->syn, sp, vel);
        return;
    }
    s->active = 1;
    s->vel = vel;
    if (E->drum_smp[d] != 255) {        /* a real-sample start: decode from its first code */
        int si = E->drum_smp[d];
        s->smp = si + 1;
        s->sPos = 0;
        s->sSub = 0;
        s->sBit = E->smp_hdr[si * 4] * 8;
        s->sYA = s->sYB = 0.0f;
        s->sStep = E->smp_f[si * 8 + 1];
    }
    for (b = lo; b <= hi; b++) {
        int x0 = E->env[off];
        s->boff[b] = off;
        s->bcode[b] = (x0 >= 1 && x0 < 32) ? E->env[off + 1] : 0;   /* L > 0, S 0 */
        off += DB_ROW_BYTES(x0);
        s->bslope[b] = bv * db_amp_code(E, s->bcode[b]) * E->grid_inv[0];
    }
    off = DB_D_POFF(d);
    for (i = 0; i < DB_MAX_PARTIALS; i++) {
        float tgt = 0.0f;
        if (i < np) {
            int x0 = E->env[off];
            s->poff[i] = off;
            s->pcode[i] = (x0 >= 1 && x0 < 32) ? E->env[off + 1] : 0;   /* L > 0, S 0 */
            off += DB_ROW_BYTES(x0);
            tgt = pv * db_amp_code(E, s->pcode[i]);
            s->pd[i] = E->part_f[(DB_D_PART(d) + i) * 3 + 1];
        }
        s->pslope[i] = tgt * E->grid_inv[0];
    }
}

DB_ALWAYS_INLINE(db_trigger_step)
static inline void db_trigger_step(DBState *st, const DBEng *E, int kit, int step, float velLo)
{
    /* st->cur: two 16-step lanes per word, K|S H|O R|C T|A L (low|high). */
    const uint32_t *p = st->cur;
    const uint8_t *kd = &E->kit_drums[kit * DB_NUM_LANES];
    uint32_t bit = 1u << (step & 15);
    uint32_t hi = bit << 16;
    float vel = (p[3] & hi) ? 1.0f : velLo;
    /* lanes hit on this step: kick, snare, chat, ohat, ride, crash, high
     * tom, low tom */
    uint32_t hits = ((p[0] & bit) ? 1u : 0u) | ((p[0] & hi) ? 2u : 0u) |
                    ((p[1] & bit) ? 4u : 0u) | ((p[1] & hi) ? 8u : 0u) |
                    ((p[2] & bit) ? 16u : 0u) | ((p[2] & hi) ? 32u : 0u) |
                    ((p[3] & bit) ? 64u : 0u) | ((p[4] & bit) ? 128u : 0u);
    int lane, v;

    hits &= st->rMask;                  /* lanes not rendered yet stay silent */
    if (hits & 4u) {                    /* a closed hat chokes the open one */
        v = 6 + st->vCur[3];
        st->vStep[v] = -st->vGain[v] * (1.0f / DB_CHOKE_SAMPLES);
    }
    /* a hit: the lane's other player plays the cache, the one that was
     * playing fades out (no click) */
    for (lane = 0; lane < DB_NUM_LANES; lane++) {
        if (!(hits & (1u << lane)) || kd[lane] == DB_NO_DRUM)
            continue;
        v = 2 * lane + st->vCur[lane];
        st->vStep[v] = -st->vGain[v] * (1.0f / DB_CHOKE_SAMPLES);
        v ^= 1;
        st->vCur[lane] ^= 1;
        st->vIdx[v] = st->cOff[lane];
        st->vRem[v] = st->cLen[lane];
        st->vGain[v] = vel;
        st->vStep[v] = 0.0f;
    }
}

/* One sample of all lanes (each plays the drum it was hit with; only the
 * one being rendered into the cache rings). Mirror: Voice.render. */
DB_ALWAYS_INLINE(db_render)
static inline float db_render(DBState *st, const DBEng *E, int bLo, int bHi)
{
    float out = 0.0f, na, nb, da, dbn;
    int lane, b, i;

    na = db_xorshift(&st->rngA);
    nb = db_xorshift(&st->rngB);
    da = na - st->xa2;
    dbn = nb - st->xb2;
    st->xa2 = st->xa1;
    st->xa1 = na;
    st->xb2 = st->xb1;
    st->xb1 = nb;
    for (b = bLo; b <= bHi; b++)
        st->g[b] = 0.0f;

    for (lane = 0; lane < DB_NUM_LANES; lane++) {
        DBLane *s = &st->lane[lane];
        int d, lo, hi, np, k;
        d = s->drum;
        if (s->kind) {
            /* synth drum: its own code, times its level and the choke fade */
            DBSyn *v = &s->syn;
            float y;
            if (!v->active) continue;
            DB_SYN_RENDER(s->kind, v, st->synp, y);
            out += y * E->drum_scale[2 * d];
            continue;
        }
        if (!s->active) continue;
        if (s->smp) {
            /* real-sample start: linear interpolation between the decoded
             * samples (rate 44.1 kHz / DB_SMP_DIV), times gain and vel,
             * faded out from fa to fb while the body's rows fade in */
            int si = s->smp - 1;
            const float *f = &E->smp_f[si * 8];
            int pos = s->sPos, fa = E->smp_hdr[si * 4 + 2], fb = E->smp_hdr[si * 4 + 3];
            float y = s->sYA + (s->sYB - s->sYA) * (float)s->sSub * (1.0f / DB_SMP_DIV);
            float gn = (pos < fa) ? 1.0f : (float)(fb - pos) * f[3];
            out += y * f[2] * s->vel * gn;
            if (++s->sSub == DB_SMP_DIV) {
                s->sSub = 0;
                s->sYA = s->sYB;
                s->sYB = db_smp_next(E, s, si);
            }
            s->sPos = ++pos;
            if (pos >= fb) s->smp = 0;
        }
        lo = DB_D_LO(d);
        hi = DB_D_HI(d);
        np = DB_D_NP(d);
        {
            /* restrict: the arrays don't overlap, so cl6x can pipeline */
            float *restrict bc = s->bcur;
            const float *restrict bs = s->bslope;
            float *restrict g = st->g;
            for (b = lo; b <= hi; b++) {
                float v = bc[b] + bs[b];
                bc[b] = v;
                g[b] += v;
            }
        }
        for (i = 0; i < np; i++) {
            const float *pf = &E->part_f[(DB_D_PART(d) + i) * 3];
            s->pd[i] *= pf[2];
            s->ph[i] = db_wrap(s->ph[i] + (pf[0] + s->pd[i]) * DB_INV_SR);
            s->pcur[i] += s->pslope[i];
            out += s->pcur[i] * db_sin(s->ph[i]);
        }
        s->t++;
        if (s->t >= E->grid[s->seg + 1]) {
            k = ++s->seg;
            if (k >= DB_NUM_SEG) {
                s->active = 0;
                continue;
            }
            {
                /* point k - 1 reached: land on it exactly, decode point k */
                float bv = E->drum_scale[2 * d] * s->vel;
                float pv = E->drum_scale[2 * d + 1] * s->vel;
                for (b = lo; b <= hi; b++) {
                    float cur = bv * db_amp_code(E, s->bcode[b]);
                    s->bcode[b] = db_code_next(E, s->boff[b], k, s->bcode[b]);
                    s->bcur[b] = cur;
                    s->bslope[b] = (bv * db_amp_code(E, s->bcode[b]) - cur) * E->grid_inv[k];
                }
                for (i = 0; i < np; i++) {
                    float cur = pv * db_amp_code(E, s->pcode[i]);
                    s->pcode[i] = db_code_next(E, s->poff[i], k, s->pcode[i]);
                    s->pcur[i] = cur;
                    s->pslope[i] = (pv * db_amp_code(E, s->pcode[i]) - cur) * E->grid_inv[k];
                }
            }
        }
    }

    /* the band-pass bank: even bands on noise A, odd bands on noise B
     * (two passes, so no per-band select; restrict for pipelining); only
     * bands bLo..bHi ring (the others have no gain: same output) */
    {
        float *restrict y1 = st->y1;
        float *restrict y2 = st->y2;
        const float *restrict g = st->g;
        const float *restrict c = E->band_coef;
        float accA = 0.0f, accB = 0.0f;
        for (b = bLo + (bLo & 1); b <= bHi; b += 2) {
            float y = c[b * 3] * da - c[b * 3 + 1] * y1[b] - c[b * 3 + 2] * y2[b];
            y2[b] = y1[b];
            y1[b] = y;
            accA += g[b] * y;
        }
        for (b = bLo + 1 - (bLo & 1); b <= bHi; b += 2) {
            float y = c[b * 3] * dbn - c[b * 3 + 1] * y1[b] - c[b * 3 + 2] * y2[b];
            y2[b] = y1[b];
            y1[b] = y;
            accB += g[b] * y;
        }
        out += accA + accB;
    }
    return out;
}

#include <c6x.h>      /* TSCL */

#ifdef DB_PROBE
/* ------------------------------------------------------------------------
 * Load probe (build.py --probe, never a release build): times every audio
 * call with the C674x time-stamp counter (TSCL, read only) and reports by
 * beeps while the Volume knob is 0. The meter sits right after DBState in
 * ctx[3]. Footswitch off clears it. Numbers, in order after a 6-s
 * pause: 1 peak call cost, 2 busiest 4096-call average (both % of the
 * time between calls, 5 % per beep), 3 ctx[3] size (64 KB per beep),
 * 4 CPU MHz (time between calls x 44100 / 8, 20 MHz per beep; 0 = TSCL
 * not running). Value v = v + 1 beeps (beeps - 1 = v in those units).
 * ---------------------------------------------------------------------- */
#define DB_PM_MAGIC 0x50524F42u   /* 'PROB' */
#define DB_PM_BYTES sizeof(DBMeter)  /* the kit cache starts after it */
#define DB_PM_MS(ms) ((int32_t)((ms) * (44.1f / 8.0f)))   /* in calls */
/* beep units of the numbers */
#define DB_PM_PCT 5.0f    /* % */
#define DB_PM_KB  64.0f   /* KB */
#define DB_PM_MHZ 20.0f   /* MHz */

typedef struct DBMeter {
    uint32_t magic;
    uint32_t last;              /* TSCL at the previous call */
    uint32_t perSum, perCnt, period;    /* ticks between calls, 4096-call mean */
    uint32_t winSum, winCnt;
    /* the numbers: [1] peak cost, [2] busiest 4096-call mean cost,
     * [3] ctx[3] KB, [4] period; scale: what turns them into the report */
    uint32_t val[5];
    float    scale[5];
    /* report: on, number, beep, tone on, calls left, the number in beep
     * units, beep in its group of 5 */
    int32_t  rep, num, beep, on, cnt, n, grp;
    float    ph;
} DBMeter;

/* one call while Volume is 0: the beeps, added to the guitar. Number
 * n = n + 1 beeps in groups of 5 (0.6 s between groups), 3 s between
 * numbers, 6 s before number 1. Timing in calls (8 samples). */
DB_ALWAYS_INLINE(db_pm_report)
static inline void db_pm_report(DBMeter *m, float *fxBuf)
{
    int i;
    if (!m->rep) {
        /* % of the period: x 100 / period, RCPSP reciprocal (no divide);
         * scales include the beep units (see DB_PM_*) */
        float p = (float)(int32_t)m->period, r = 0.0f;
        if (m->period) {
            r = _rcpsp(p);
            r = r * (2.0f - p * r);
            r = r * (2.0f - p * r);
        }
        m->scale[1] = m->scale[2] = (100.0f / DB_PM_PCT) * r;
        m->scale[3] = 1.0f / DB_PM_KB;
        m->scale[4] = 0.0055125f / DB_PM_MHZ;
        m->rep = 1;
        m->num = 4;             /* "end of number 4": the 6 s pause, then number 1 */
        m->beep = m->n = m->grp = 0;
        m->on = 1;
        m->cnt = 1;
    }
    if (--m->cnt <= 0) {
        m->on ^= 1;
        m->cnt = DB_PM_MS(120);
        if (!m->on) {
            /* the gap after a beep, then the next beep / number */
            m->cnt = DB_PM_MS(250);
            if (++m->grp == 5) {
                m->grp = 0;
                m->cnt = DB_PM_MS(600);
            }
            if (++m->beep > m->n) {
                m->beep = m->grp = 0;
                m->cnt = DB_PM_MS(3000);
                if (++m->num > 4) {
                    m->num = 1;
                    m->cnt = DB_PM_MS(6000);
                }
                m->n = (int32_t)((float)(int32_t)m->val[m->num] * m->scale[m->num] + 0.5f);
                if (m->n > 60) m->n = 60;
            }
        }
    }
    for (i = 0; i < 8; i++) {
        float y = 0.0f;
        if (m->on) {
            m->ph = db_wrap(m->ph + 880.0f * DB_INV_SR);
            y = (m->ph < 0.5f) ? 0.15f : -0.15f;
        }
        fxBuf[i] += y;
        fxBuf[i + 8] += y;
    }
}

/* time between calls; called at the start of a measured call */
DB_ALWAYS_INLINE(db_pm_tick)
static inline void db_pm_tick(DBMeter *m, uint32_t now)
{
    uint32_t d = now - m->last;
    m->last = now;
    if (d == 0u || d > 4000000u) return;    /* stopped timer or a gap */
    m->perSum += d;
    if (++m->perCnt == 4096u) {
        m->period = m->val[4] = m->perSum >> 12;
        m->perSum = m->perCnt = 0u;
    }
}

/* this call's cost, once the period is known */
DB_ALWAYS_INLINE(db_pm_cost)
static inline void db_pm_cost(DBMeter *m, uint32_t cost)
{
    if (m->period == 0u) return;
    if (cost > m->val[1]) m->val[1] = cost;
    m->winSum += cost;
    if (++m->winCnt == 4096u) {
        uint32_t a = m->winSum >> 12;
        if (a > m->val[2]) m->val[2] = a;
        m->winSum = m->winCnt = 0u;
    }
}
#endif

#ifndef DB_PROBE
#define DB_PM_BYTES 0u
#endif

void DB_AUDIO_FUNC(unsigned int *ctx)
{
    uint32_t t0 = TSCL;         /* the render's time budget starts here */
#ifdef DB_PROBE
    DBMeter *pm = 0;
#endif
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf = ZDL_PTR(float *, ctx[5]);

    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *, *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    *magicDst = *magicSrc;

    volatile unsigned int *desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    uintptr_t base = (uintptr_t)desc[0];
    uintptr_t end = (uintptr_t)desc[1];
    unsigned int span = desc[2];
    uintptr_t stateBase = db_align4(base);
    uintptr_t bytes = end - base;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if (bytes < sizeof(DBState) || span < bytes) return;
    if (stateBase + sizeof(DBState) > end) return;

    DBState *st = (DBState *)stateBase;
    if (st->magic != DB_MAGIC || st->version != DB_VERSION) {
        db_init_state(st);
        return;
    }
    /* Start gate: away from the slot (patch switch) -> stop the song and
     * the players, check the whole cache before starting again */
    if (t0 - st->lastCall > DB_AWAY_CYC) {
        int v;
        st->running = 0u;
        st->ckWait = 1;
        st->ckPos = 0;
        st->ckAcc = 0u;
        st->sinceCrash = -1;    /* the song's first downbeat: no fill crash */
        for (v = 0; v < 2 * DB_NUM_LANES; v++)
            st->vRem[v] = 0;
        /* half rendered (unchecked): cKit -1, render again (no branch: smaller) */
        st->cKit |= (st->rLane - DB_NUM_LANES) >> 31;
    }
    st->lastCall = t0;

#ifdef DB_PROBE
    if (stateBase + sizeof(DBState) + sizeof(DBMeter) <= end) {
        volatile uint32_t *w;
        int k;
        pm = (DBMeter *)(stateBase + sizeof(DBState));
        if (pm->magic != DB_PM_MAGIC || params[0] < 0.5f) {
            w = (volatile uint32_t *)pm;
            for (k = 0; k < (int)(sizeof(DBMeter) / 4); k++)
                w[k] = 0u;
            pm->magic = DB_PM_MAGIC;
            pm->last = t0;
            pm->val[3] = (uint32_t)bytes >> 10;
        } else {
            db_pm_tick(pm, t0);
        }
    }
#endif

    /* Pages: BeatA, FillA, LenA / BeatB, FillB, LenB / Tempo, Volume, Kit. */
    float beatAN   = db_param_list(params[DRUMBOX_BEATA_SLOT], DRUMBOX_BEATA_DEFAULT_NORM);
    float fillAN   = db_param_list(params[DRUMBOX_FILLA_SLOT], DRUMBOX_FILLA_DEFAULT_NORM);
    float lenAN    = db_param(params[DRUMBOX_LENA_SLOT],       DRUMBOX_LENA_DEFAULT_NORM);
    float beatBN   = db_param_list(params[DRUMBOX_BEATB_SLOT], DRUMBOX_BEATB_DEFAULT_NORM);
    float fillBN   = db_param_list(params[DRUMBOX_FILLB_SLOT], DRUMBOX_FILLB_DEFAULT_NORM);
    float lenBN    = db_param(params[DRUMBOX_LENB_SLOT],       DRUMBOX_LENB_DEFAULT_NORM);
    /* Tempo raw = knob / 100 up to 2.0 (max 200, handler law 2.0f / 201). */
    float tempoR  = params[DRUMBOX_TEMPO_SLOT];
    if (tempoR != tempoR || tempoR < 0.0f) tempoR = DRUMBOX_TEMPO_DEFAULT_NORM;
    tempoR = db_clamp(tempoR, 0.0f, DRUMBOX_TEMPO_UI_MAX * 0.01f);
    float levelN  = db_param(params[DRUMBOX_VOLUME_SLOT],  DRUMBOX_VOLUME_DEFAULT_NORM);
    float kitN    = db_param(params[DRUMBOX_KIT_SLOT],     DRUMBOX_KIT_DEFAULT_NORM);
#ifdef DB_PROBE
    /* Volume 0: no drums, beep the numbers (the sequencer waits) */
    if (pm) {
        if (levelN < 0.005f) {
            db_pm_report(pm, fxBuf);
            return;
        }
        pm->rep = 0;
    }
#endif

    float bpm = DRUMBOX_TEMPO_AUDIO_MIN + tempoR *
        (100.0f * (DRUMBOX_TEMPO_AUDIO_MAX - DRUMBOX_TEMPO_AUDIO_MIN) / DRUMBOX_TEMPO_UI_MAX);
    float inc = bpm * DB_STEP_PER_BPM;
    float drumGain = levelN * 1.3f;   /* 1.3: dense patterns with cymbals peak ~0.9 at Volume 70 */
    /* One beat / fill per knob step, knob 0..150 (values past the end give the last one);
     * must match knob_to_beat() / knob_to_fill() in patterns.py and the names. */
    int beatA = (int)(beatAN * 100.0f + 0.5f);
    int fillA = (int)(fillAN * 100.0f + 0.5f);
    int beatB = (int)(beatBN * 100.0f + 0.5f);
    int fillB = (int)(fillBN * 100.0f + 0.5f);
    if (beatA > DB_NUM_BEATS - 1) beatA = DB_NUM_BEATS - 1;
    if (fillA > DB_NUM_FILLS - 1) fillA = DB_NUM_FILLS - 1;
    if (beatB > DB_NUM_BEATS - 1) beatB = DB_NUM_BEATS - 1;
    if (fillB > DB_NUM_FILLS - 1) fillB = DB_NUM_FILLS - 1;
    /* Kit: one knob step per kit (knob max = DB_NUM_KITS - 1, raw = v / 100);
     * must match knob_to_kit() in kits.py and the name. */
    int kitKnob = (int)(kitN * 100.0f + 0.5f);
    /* LenA / LenB: one knob step per entry; bars of each section and
     * whether its fill also plays in the middle. */
    int lenA = DB_LENGTH_FROM_KNOB((int)(lenAN * 100.0f + 0.5f));
    int lenB = DB_LENGTH_FROM_KNOB((int)(lenBN * 100.0f + 0.5f));
    int barsA = lenA & 255, midA = lenA >> 8;
    int barsB = lenB & 255, midB = lenB >> 8;
    int kit = DB_KIT_FROM_KNOB(kitKnob);
    /* The engine tables' address goes through the state with a volatile
     * read, so cl6x loads it once instead of rebuilding it (MVKL/MVKH +
     * a .rela.dyn entry) at every inlined use. */
    const DBEng *E;
    st->eng = &db_eng;
    E = *(const DBEng * volatile *)&st->eng;

    /* The kit cache (after the state, and the probe's meter): a new kit
     * stops the song and the players and renders again from lane 0 (the
     * start gate restarts the song at beat 1 once it is ready); then render for
     * DB_RENDER_CYC cycles of this call (footswitch off too, so the kit
     * gets ready).
     * Mirror: KitCache in preview.py. */
    int16_t *cache = (int16_t *)(stateBase + sizeof(DBState) + DB_PM_BYTES);
    int32_t cacheCap = ((uintptr_t)cache < end) ? (int32_t)((end - (uintptr_t)cache) >> 1) : 0;
    int i;
    if (kit != st->cKit) {
        st->cKit = kit;
        st->running = 0u;
        st->rLane = st->rPos = st->rEnd = 0;
        st->rMask = 0u;
        st->ckOk = st->ckPos = st->ckWait = 0;
        st->ckAcc = 0u;
        st->sinceCrash = -1;    /* the song's first downbeat: no fill crash */
        for (i = 0; i < 2 * DB_NUM_LANES; i++) {
            st->vRem[i] = 0;
            st->vCur[i >> 1] = 0;
        }
    }
    for (i = 0; i < DB_RENDER_MAX && st->rLane < DB_NUM_LANES &&
                (uint32_t)(TSCL - t0) < DB_RENDER_CYC; i++) {
        int r = st->rLane, l = DB_RENDER_LANE(r), d = E->kit_drums[kit * DB_NUM_LANES + l];
        int32_t at = st->rEnd + st->rPos;
        float y;
        if (st->rPos == 0) {
            db_reset_engine(st, l);
            st->rLast = -1;
            st->cOff[l] = st->rEnd;
            st->rBLo = 1;
            st->rBHi = 0;
            if (d != DB_NO_DRUM) {
                if (DB_D_KIND(d)) {
                    /* its params from the palette (one lane renders at a time) */
                    volatile float *f = st->synp;
                    const uint8_t *ix = &E->syn_idx[DB_D_BOFF(d)];
                    int j;
                    for (j = 0; j < DB_SYN_NP; j++)
                        f[j] = E->syn_pal[ix[j]];
                }
                db_hit(E, &st->lane[l], d, 1.0f, st->synp);
                if (!DB_D_KIND(d)) {
                    st->rBLo = DB_D_LO(d);
                    st->rBHi = DB_D_HI(d);
                }
            }
        }
        /* a drum ends when the engine is done, after DB_QUIET quiet
         * samples or when the cache is full; its length = up to its last
         * loud sample */
        if (d == DB_NO_DRUM || at >= cacheCap || st->rPos - st->rLast > DB_QUIET ||
            (st->rPos > 0 && !st->lane[l].active && !st->lane[l].syn.active)) {
            st->cLen[l] = st->rLast + 1;
            st->rEnd += st->rLast + 1;
            st->rLane = r + 1;
            st->rMask |= 1u << l;
            st->rPos = 0;
            st->lane[l].active = 0;     /* stopped (it may still ring) */
            st->lane[l].syn.active = 0;
            continue;
        }
        y = db_clamp(db_render(st, E, st->rBLo, st->rBHi) * DB_CACHE_ONE, -32767.0f, 32767.0f);
        cache[at] = (int16_t)(int32_t)y;
        if (db_abs(y) >= DB_LOUD) st->rLast = st->rPos;
        st->rPos++;
    }
    if (st->rLane >= DB_NUM_LANES) {
        int32_t p = st->ckPos, e = p + (st->ckWait ? DB_CHECK_FAST : DB_CHECK_N);
        uint32_t acc = st->ckAcc;
        if (e > st->rEnd) e = st->rEnd;
        for (; p < e; p++)
            acc += (uint32_t)(uint16_t)cache[p];
        if (p >= st->rEnd) {
            if (!st->ckOk) {
                st->ckRef = acc;
                st->ckOk = 1;
            } else if (acc != st->ckRef) {
                st->cKit = -1;          /* overwritten: render the kit again */
            }
            st->ckWait = 0;
            p = 0;
            acc = 0u;
        }
        st->ckPos = p;
        st->ckAcc = acc;
    }

    /* Footswitch off: stop the sequencer so the next ON restarts at beat 1. */
    if (params[0] < 0.5f) {
        st->running = 0u;
        return;
    }

    if (!st->running) {
        /* Start gate: wait (guitar only) until the closed hat is in the
         * cache and, after a gap, the cache check has passed; with lanes
         * still rendering the bars are count-in bars (below) */
        if (!(st->rMask & 4u) || st->ckWait) return;
        /* Park at the end of the last step so the next sample fires step 0. */
        st->running = 1u;
        st->countN = -1;
        st->step = 15;
        st->sect = 1;           /* past section B's end: the next bar is section A's bar 0 */
        st->bar = barsB;
        st->swing = 0.0f;       /* bar 0 sets the beat's swing */
        st->phase = 1.0f;
        /* the song's first bar follows section B's last: FillB's crash
         * starts it (FillA's when LenB is Skip: section A loops alone),
         * but only on a footswitch start: after init, a kit change or a
         * patch switch (sinceCrash -1) the first downbeat has no crash */
        {
            int f = db_resolve_fill(st, (barsB == 0) ? fillA : fillB);
            st->lastFill = (st->sinceCrash < 0) ? 0 : f;
        }
        st->sinceCrash = DB_CRASH_GAP;
        st->randBeat = -1;      /* Randm picks a new beat at bar 0 */
    }

    for (i = 0; i < 8; i++) {
        float len = ((uint32_t)st->step & st->swMask) ? (1.0f - st->swing) : (1.0f + st->swing);
        st->phase += inc;
        if (st->phase >= len) {
            st->phase -= len;
            st->step = (st->step + 1) & 15;
            if (st->step == 0 && (st->rLane < DB_NUM_LANES ||
                                  (st->countN > 1 && !(st->countN & 1)))) {
                /* count-in bar (the kit is still rendering, or to make the
                 * count 1 or even bars): closed hat quarters, beat 1
                 * accented; the song state waits */
                st->countN++;
                st->cur[0] = st->cur[2] = st->cur[4] = 0u;
                st->cur[1] = 0x1111u;
                st->cur[3] = 0x10000u;
            } else if (st->step == 0) {
                /* new bar: knob changes take effect here; past the
                 * section's end (also after a shorter Length) the other
                 * section starts, unless its Length is Skip (0 bars: then
                 * this one starts again); a section turned to Skip ends at once */
                int bars, fill, b, prev;
                int skipA = (barsA == 0), skipB = (barsB == 0);
                st->bar++;
                if (st->bar >= (st->sect ? barsB : barsA)) {
                    if (!(st->sect ? skipA : skipB)) st->sect ^= 1;
                    st->bar = 0;
                }
                if ((st->sect ? skipB : skipA) && !(st->sect ? skipA : skipB)) {
                    st->sect ^= 1;
                    st->bar = 0;
                }
                bars = st->sect ? barsB : barsA;
                b = st->sect ? beatB : beatA;
                /* both Skip: no drums, no fills, no crash */
                if (bars == 0) {
                    b = 0;
                    fill = 0;
                    st->lastFill = 0;
                } else {
                    fill = db_resolve_fill(st, db_section_fill(st->bar, bars, st->sect ? midB : midA,
                                                           st->sect ? fillB : fillA));
                }
                /* Randm: a new beat at each start of the section, or as
                 * soon as the knob reaches Randm (randBeat -1) */
                if (b == DB_BEAT_RANDOM) {
                    if (st->bar == 0 || st->randBeat < 0)
                        st->randBeat = db_pick_beat(st);
                    b = st->randBeat;
                } else {
                    st->randBeat = -1;
                }
                /* the previous bar's crash fill crashes this downbeat
                 * unless the last crash is under DB_CRASH_GAP bars back;
                 * a middle fill never crashes (lastFill 0) */
                st->sinceCrash++;
                prev = (st->sinceCrash >= DB_CRASH_GAP) ? st->lastFill : 0;
                db_compose_bar(st, b, fill, prev);
                if (st->cur[2] & 0x10000u) st->sinceCrash = 0;
                st->lastFill = (st->bar == bars - 1) ? fill : 0;
                /* the beat's swing, from word 4's upper bits (fills keep them) */
                {
                    uint32_t c = (st->cur[4] >> 16) & 3u;
                    st->swing = DB_SWING_AMT(c);
                    st->swMask = DB_SWING_MASK(c);
                }
            }
            db_trigger_step(st, E, kit, st->step, DB_VEL_LO);
        }

        /* the players: cached drums x velocity, faded when choked or
         * retriggered */
        float mix = 0.0f;
        int v;
        for (v = 0; v < 2 * DB_NUM_LANES; v++) {
            if (st->vRem[v] <= 0) continue;
            mix += (float)cache[st->vIdx[v]++] * st->vGain[v];
            st->vRem[v]--;
            if (st->vStep[v] != 0.0f) {
                st->vGain[v] += st->vStep[v];
                if (st->vGain[v] <= 0.0f) st->vRem[v] = 0;
            }
        }
        float drum = db_soft_clip(mix * (1.0f / DB_CACHE_ONE)) * drumGain;
        fxBuf[i]     += drum;          /* guitar passes at full level */
        fxBuf[i + 8] += drum;
    }
#ifdef DB_PROBE
    if (pm) db_pm_cost(pm, TSCL - t0);
#endif
}

/* ------------------------------------------------------------------------
 * Knob display callbacks (descriptor getstr_ptr, set via manifest "getstr").
 * The pedal UI calls them with the raw knob value v (0..max) and a text
 * buffer; they write a NUL-terminated string and return its length.
 * Names come from static const tables in .const (drumbox_patterns.h /
 * drumbox_kits.h, addresses fixed by the pedal via .rela.dyn); no string
 * literals, loops, division or calls.
 * ---------------------------------------------------------------------- */

/* Tempo: v 0..200 -> "40".."240" (BPM = 40 + v, same as the DSP; the
 * constant division folds at compile time). */
int GetString_DbTempo(unsigned int v, char *s)
{
    unsigned int bpm, h = 0u, t = 0u;
    int n = 0;
    if (v > DRUMBOX_TEMPO_UI_MAX) v = DRUMBOX_TEMPO_UI_MAX;
    bpm = 40u + v * (200u / DRUMBOX_TEMPO_UI_MAX);
    /* Digits by compare-and-subtract, no loops: TI pipelines loops with
     * DINT/RINT, which has no place in a UI callback. */
    if (bpm >= 200u) { bpm -= 200u; h = 2u; }
    else if (bpm >= 100u) { bpm -= 100u; h = 1u; }
    if (bpm >= 80u) { bpm -= 80u; t += 8u; }
    if (bpm >= 40u) { bpm -= 40u; t += 4u; }
    if (bpm >= 20u) { bpm -= 20u; t += 2u; }
    if (bpm >= 10u) { bpm -= 10u; t += 1u; }
    if (h) s[n++] = (char)('0' + h);
    s[n++] = (char)('0' + t);
    s[n++] = (char)('0' + bpm);
    s[n] = 0;
    return n;
}

/* Beat: v 0..100 -> beat name (generated from patterns.py). */
int GetString_DbBeat(unsigned int v, char *s)
{
    DB_BEAT_GETSTR_BODY(v, s);
}

/* FillA and FillB (one callback for both knobs): v -> fill name. */
int GetString_DbFill(unsigned int v, char *s)
{
    DB_FILL_GETSTR_BODY(v, s);
}

/* Kit: v 0..100 -> kit name (generated from kits.py). */
int GetString_DbKit(unsigned int v, char *s)
{
    DB_KIT_GETSTR_BODY(v, s);
}

/* LenA and LenB: v -> "1Bar".."16Bar" (generated from patterns.py). */
int GetString_DbLength(unsigned int v, char *s)
{
    DB_LENGTH_GETSTR_BODY(v, s);
}
