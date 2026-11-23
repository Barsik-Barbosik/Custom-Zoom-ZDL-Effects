/* melopack: the Melody Box storage format in C (mirror of melopack.py,
 * whose module docstring is the spec): the decoder (mp_unpack) and the
 * packer (mp_start + mp_run, a job a few units per call).
 *
 * Grid slot values: MP_REST, MP_HOLD, MP_NOTE + pitch (pitch 0..36).
 * Floppy words: 4 per part, part q in words[4 q .. 4 q + 3], its bits
 * 128 q .. 128 q + 105; bits 0..3 = the key, then 3-bit tokens (34 per part,
 * a part's tokens in its bits 4..105).
 *
 * Rules (as the effects): no integer divide, no stack arrays (the grid is
 * the caller's), no calls between sections: mp_unpack is out of line in
 * .text (MP_COLD), its helpers force-inlined. Loops that store into the
 * grid are not unrolled (UNROLL(1)) and the grid is volatile, so no
 * DINT / RINT blocks. Junk words decode to a clean grid too (as in the
 * model): pitches fold back by octaves, cur is clamped, a copy or rhythm
 * reaching before slot 0 is skipped, a hold after a rest or at slot 0 reads
 * as a rest. */
#ifndef MELOPACK_H
#define MELOPACK_H
#include <stdint.h>

#define MP_DO_PRAGMA(x) _Pragma(#x)
#define MP_EXPAND_PRAGMA(x) MP_DO_PRAGMA(x)
#define MP_INLINE(f) MP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(f))
#ifndef MP_COLD
#define MP_COLD(f) MP_EXPAND_PRAGMA(FUNC_CANNOT_INLINE(f)) MP_EXPAND_PRAGMA(CODE_SECTION(f, ".text")) MP_EXPAND_PRAGMA(FUNC_INTERRUPT_THRESHOLD(f, 1))
#endif

#define MP_MAXN      128u               /* most slots */
#define MP_NPITCH    37u                /* pitch 0..36 */
#define MP_REST      0u
#define MP_HOLD      1u
#define MP_NOTE      2u                 /* a note = MP_NOTE + pitch */
#define MP_KEY_BITS  4u
#define MP_NTOK      34u                /* 3-bit tokens per part */
#define MP_START_DEG 7                  /* tonic of the middle octave */
#define MP_CUR_MIN   (-14)
#define MP_CUR_MAX   28
#define MP_L_DEFAULT 1u                 /* LEN index: 2 slots */

#define MP_ESC       0u                 /* main token 0 */
#define MP_E_END     0u                 /* ESC codes */
#define MP_E_REST    1u
#define MP_E_LEN     2u
#define MP_E_RHY     3u
#define MP_E_SHARP   4u
#define MP_E_JUMP    5u
#define MP_E_COPY    6u
#define MP_E_EXT     7u

/* The tables in one object (one .const section). */
static const struct {
    uint8_t len[8];                     /* LEN / EXTEND, slots */
    uint8_t cp[8];                      /* COPY / RHYTHM length, slots */
    uint8_t deg_ofs[7];                 /* major scale */
    uint8_t pc_deg[12];                 /* semitone above the key -> degree | sharp << 3 */
} mp_t = {
    { 1, 2, 3, 4, 6, 8, 12, 16 },
    { 4, 6, 8, 12, 16, 24, 32, 48 },
    { 0, 2, 4, 5, 7, 9, 11 },
    { 0, 0 | 8, 1, 1 | 8, 2, 3, 3 | 8, 4, 4 | 8, 5, 5 | 8, 6 },
};

/* Degree (any integer) -> semitones above the key's tonic of octave 0. */
MP_INLINE(mp_deg_semis)
static inline int mp_deg_semis(int d)
{
    int o = 0;
#pragma UNROLL(1)
    while (d < 0) { d += 7; o -= 12; }
#pragma UNROLL(1)
    while (d >= 7) { d -= 7; o += 12; }
    return o + (int)mp_t.deg_ofs[d];
}

/* Pitch -> degree in the key (*sharp: 1 if a semitone above a degree). */
MP_INLINE(mp_pitch_deg)
static inline int mp_pitch_deg(int p, int key, int *sharp)
{
    int r = p - key, o = 0, c;
#pragma UNROLL(1)
    while (r < 0) { r += 12; o -= 7; }
#pragma UNROLL(1)
    while (r >= 12) { r -= 12; o += 7; }
    c = (int)mp_t.pc_deg[r];
    *sharp = c >> 3;
    return o + (c & 7);
}

/* The 3-bit token at bit address a (a < 4 parts x 128). */
MP_INLINE(mp_get3)
static inline uint32_t mp_get3(const uint32_t *w, uint32_t a)
{
    uint32_t lo = w[a >> 5] >> (a & 31u);
    if ((a & 31u) > 29u) lo |= w[(a >> 5) + 1u] << (32u - (a & 31u));
    return lo & 7u;
}

/* Bit address of the token after the one at a. */
MP_INLINE(mp_next)
static inline uint32_t mp_next(uint32_t a)
{
    a += 3u;
    if ((a & 127u) == 106u) a += 26u;
    return a;
}

/* Floppy words (4 per part, parts 1..4) -> grid g[0 .. n - 1]; returns n
 * (16, 32, 64 or 128); *step8 = the step length flag (0 = 1/16, 1 = 1/8). */
MP_COLD(mp_unpack)
static uint32_t mp_unpack(const uint32_t *w, uint32_t parts, volatile uint8_t *g, uint32_t *step8)
{
    uint32_t key, ntok, nt = 0u, a = MP_KEY_BITS, n, dt, p = 0u, L, rleft = 0u, rdist = 0u;
    uint32_t t, e, x, i, dist, ln, v;
    int cur = MP_START_DEG, d, pitch, sharp = 0, s, sx;

    key = w[0] & 15u;
    if (key > 11u) key = 0u;
    ntok = MP_NTOK * parts;
#pragma UNROLL(1)
    for (i = 0u; i < MP_MAXN; i++) g[i] = MP_REST;

    x = mp_get3(w, a); a = mp_next(a); nt++;
    n = 16u << (x & 3u);
    *step8 = x >> 2 & 1u;
    dt = n <= 64u ? 2u : 3u;
    L = mp_t.len[MP_L_DEFAULT];
#pragma UNROLL(1)
    while (p < n) {
        if (rleft) {
            v = g[p - rdist];
            if (v < MP_NOTE) {          /* rest / hold from the rhythm */
                g[p] = (v == MP_HOLD && g[p - 1u] != MP_REST) ? MP_HOLD : MP_REST;
                p++;
                rleft--;
                continue;
            }
        }
        if (nt >= ntok) break;
        t = mp_get3(w, a); a = mp_next(a); nt++;
        if (t != MP_ESC) {              /* a note, degree step t - 4 */
            d = cur + (int)t - 4;
            pitch = (int)key + mp_deg_semis(d) + sharp;
#pragma UNROLL(1)
            while (pitch >= (int)MP_NPITCH) { pitch -= 12; d -= 7; }
#pragma UNROLL(1)
            while (pitch < 0) { pitch += 12; d += 7; }
            cur = d;
            sharp = 0;
            g[p++] = (uint8_t)(MP_NOTE + (uint32_t)pitch);
            if (rleft) {
                rleft--;
                continue;
            }
#pragma UNROLL(1)
            for (i = 1u; i < L && p < n; i++) g[p++] = MP_HOLD;
            continue;
        }
        if (nt >= ntok) break;
        e = mp_get3(w, a); a = mp_next(a); nt++;
        if (e == MP_E_SHARP) {
            sharp = 1;
            continue;
        }
        if (e != MP_E_JUMP) rleft = 0u;     /* any other command ends a rhythm */
        if (e == MP_E_END) break;
        if (e == MP_E_REST) {
#pragma UNROLL(1)
            for (i = 0u; i < L && p < n; i++) g[p++] = MP_REST;
        } else if (e == MP_E_RHY || e == MP_E_COPY) {
            if (nt + dt + (e == MP_E_COPY ? 2u : 1u) > ntok) break;
            dist = 0u;
#pragma UNROLL(1)
            for (i = 0u; i < dt; i++) {
                dist |= mp_get3(w, a) << (3u * i);
                a = mp_next(a);
                nt++;
            }
            dist++;
            ln = mp_t.cp[mp_get3(w, a)]; a = mp_next(a); nt++;
            s = 0;
            if (e == MP_E_COPY) {
                s = (int)mp_get3(w, a) - 3; a = mp_next(a); nt++;
                if (s == 4) s = 0;
            }
            if (dist > p) continue;
            if (e == MP_E_RHY) {
                rleft = ln;
                rdist = dist;
                continue;
            }
#pragma UNROLL(1)
            for (i = 0u; i < ln && p < n; i++) {
                v = g[p - dist];
                if (v >= MP_NOTE && s) {    /* moved by s scale degrees, sharp kept */
                    d = mp_pitch_deg((int)v - (int)MP_NOTE, (int)key, &sx);
                    pitch = (int)key + mp_deg_semis(d + s) + sx;
#pragma UNROLL(1)
                    while (pitch >= (int)MP_NPITCH) pitch -= 12;
#pragma UNROLL(1)
                    while (pitch < 0) pitch += 12;
                    v = MP_NOTE + (uint32_t)pitch;
                } else if (v == MP_HOLD && g[p - 1u] == MP_REST)
                    v = MP_REST;
                g[p++] = (uint8_t)v;
            }
            /* cur = the degree of the last note before p (none: the start) */
            cur = MP_START_DEG;
#pragma UNROLL(1)
            for (i = p; i > 0u; i--)
                if (g[i - 1u] >= MP_NOTE) {
                    cur = mp_pitch_deg((int)g[i - 1u] - (int)MP_NOTE, (int)key, &sx);
                    break;
                }
        } else {                        /* LEN, JUMP, EXTEND: one more token */
            if (nt >= ntok) break;
            x = mp_get3(w, a); a = mp_next(a); nt++;
            if (e == MP_E_LEN)
                L = mp_t.len[x];
            else if (e == MP_E_JUMP) {
                cur += x < 4u ? (int)x - 7 : (int)x;
                if (cur < MP_CUR_MIN) cur = MP_CUR_MIN;
                if (cur > MP_CUR_MAX) cur = MP_CUR_MAX;
            } else {                    /* EXTEND */
                v = (p > 0u && g[p - 1u] != MP_REST) ? MP_HOLD : MP_REST;
#pragma UNROLL(1)
                for (i = 0u; i < mp_t.len[x] && p < n; i++) g[p++] = (uint8_t)v;
            }
        }
    }
    return n;
}

/* ------------------------------------------------------------------ packer
 * mp_start + mp_run: the model's pack() as a job, a few units per audio
 * call (a unit: one key's base tables, one row of one match pass, one DP
 * slot, or the emit). Same loops, orders and strict comparisons as the
 * model, so the same key and tokens come out (the self-test compares the
 * words). Per key: base tables (note bits, prefix sums, runs), 8 match
 * passes (COPY with shift 0, -1, 1, -2, 2, -3, 3, then RHYTHM), the DP over
 * slots 0..n with back-pointers; the cheapest key's tables stay in one of
 * two buffers. Then the path's moves -> tokens -> words. */

#define MP_INF       0xFFFFu
#define MP_C_SET     3u
#define MP_C_ESC     6u
#define MP_C_LEN     9u
#define MP_C_JUMP    9u
#define MP_C_EXT     9u
#define MP_PART_BITS 102u               /* token bits per part */
#define MP_MAXPARTS  4u
#define MP_NPASS     8u                 /* 7 COPY shifts + RHYTHM */
#define MP_MV_N 1u                      /* move kinds (0 = none) */
#define MP_MV_R 2u
#define MP_MV_X 3u
#define MP_MV_C 4u
#define MP_MV_Y 5u
/* a move: kind | p << 3 | li << 11 | x << 14 (from-state p, li; x = table index) */
#define MP_MV(k, p, li, x) ((k) | (p) << 3 | (li) << 11 | (x) << 14)

/* Per key: tables and the DP's back-pointers. */
typedef struct {
    uint8_t nc[MP_MAXN + 1];            /* note bits at p (0 elsewhere) */
    uint16_t pre[MP_MAXN + 1];          /* prefix sums of nc */
    uint8_t note_len[MP_MAXN + 1], hold_run[MP_MAXN + 1], rest_run[MP_MAXN + 1];
    uint8_t cmax[MP_MAXN + 1], cdist[MP_MAXN + 1], rmax[MP_MAXN + 1], rsrc[MP_MAXN + 1];
    int8_t cshift[MP_MAXN + 1];         /* longest COPY from p: length, distance, shift */
    uint32_t frm[MP_MAXN + 1][8];       /* back-pointer per (slot, length index) */
    uint32_t best, bp, bli, key;        /* cheapest bits, its end state, the key */
} MpTab;

typedef struct {
    uint32_t phase;                     /* MP_PH_* */
    uint32_t n, step8, key, pass, p, cur, bidx, bcost;
    uint8_t g[MP_MAXN];                 /* the grid (normalised) */
    uint8_t row[2][MP_MAXN + 1];        /* match runs: rows p + 1 and p */
    uint8_t sh[MP_NPITCH + 2];          /* this pass: shifted value, 0xFF = none */
    uint16_t cost[MP_MAXN + 1][8];
    uint32_t moves[MP_MAXN + 1];
    MpTab tab[2];
    /* result (phase MP_PH_DONE) */
    uint32_t words[4u * MP_MAXPARTS];
    uint32_t bits, parts, okey, err;    /* err: emitted bits != the DP's */
} MpJob;

#define MP_PH_INIT 1u
#define MP_PH_RUNS 2u
#define MP_PH_DP   3u
#define MP_PH_EMIT 4u
#define MP_PH_DONE 5u

/* Bits of a degree step: one note token, after a JUMP per 7 beyond +-3. */
MP_INLINE(mp_interval_bits)
static inline uint32_t mp_interval_bits(int i)
{
    uint32_t b = 3u;
#pragma UNROLL(1)
    while (i > 3) { i -= i > 7 ? 7 : i; b += MP_C_JUMP; }
#pragma UNROLL(1)
    while (i < -3) { i -= i < -7 ? -7 : i; b += MP_C_JUMP; }
    return b;
}

/* Shift of match pass ps (0..6): 0, -1, 1, -2, 2, -3, 3. */
MP_INLINE(mp_pass_shift)
static inline int mp_pass_shift(uint32_t ps)
{
    return (ps & 1u) ? -(int)((ps + 1u) >> 1) : (int)(ps >> 1);
}

/* Start a pack job: grid g (n = 16, 32, 64 or 128 slots), step flag. */
MP_COLD(mp_start)
static void mp_start(MpJob *j, const volatile uint8_t *g, uint32_t n, uint32_t step8)
{
    volatile MpJob *vj = (volatile MpJob *)j;
    uint32_t i, v, prev = MP_REST;
#pragma UNROLL(1)
    for (i = 0u; i < n; i++) {          /* normalise */
        v = g[i];
        if (v > MP_NPITCH + 1u || (v == MP_HOLD && prev == MP_REST)) v = MP_REST;
        vj->g[i] = (uint8_t)v;
        prev = v;
    }
    vj->n = n;
    vj->step8 = step8;
    vj->key = 0u;
    vj->cur = 0u;
    vj->bidx = 0u;
    vj->bcost = 0xFFFFFFFFu;
    vj->err = 0u;
    vj->phase = MP_PH_INIT;
}

/* Key j->key's base tables into tab[j->cur]. */
MP_INLINE(mp_tables)
static inline void mp_tables(volatile MpJob *j)
{
    volatile MpTab *t = &j->tab[j->cur];
    uint32_t n = j->n, p, q, v;
    int cur = MP_START_DEG, d, s;
    t->key = j->key;
    t->nc[n] = 0u;
    t->pre[0] = 0u;
#pragma UNROLL(1)
    for (p = 0u; p < n; p++) {
        if (j->g[p] >= MP_NOTE) {
            d = mp_pitch_deg((int)j->g[p] - (int)MP_NOTE, (int)j->key, &s);
            t->nc[p] = (uint8_t)(mp_interval_bits(d - cur) + (s ? MP_C_ESC : 0u));
            cur = d;
        } else
            t->nc[p] = 0u;
        t->pre[p + 1u] = (uint16_t)(t->pre[p] + t->nc[p]);
    }
    t->note_len[n] = 0u;
    t->hold_run[n] = 0u;
    t->rest_run[n] = 0u;
#pragma UNROLL(1)
    for (p = n; p > 0u; p--) {
        q = p - 1u;
        v = j->g[q];
        t->hold_run[q] = v == MP_HOLD ? (uint8_t)(t->hold_run[p] + 1u) : 0u;
        t->rest_run[q] = v == MP_REST ? (uint8_t)(t->rest_run[p] + 1u) : 0u;
        t->note_len[q] = v >= MP_NOTE ? (uint8_t)(1u + t->hold_run[p]) : 0u;
    }
#pragma UNROLL(1)
    for (p = 0u; p <= n; p++) {
        t->cmax[p] = 0u;
        t->cdist[p] = 0u;
        t->cshift[p] = 0;
        t->rmax[p] = 0u;
        t->rsrc[p] = 0u;
    }
}

/* Set up match pass j->pass (0..6: COPY shifts; 7: RHYTHM): the shift map,
 * an empty row, p = n - 1. */
MP_INLINE(mp_pass_start)
static inline void mp_pass_start(volatile MpJob *j)
{
    uint32_t v;
    int s = j->pass >= 7u ? 0 : mp_pass_shift(j->pass), d, x, q;
#pragma UNROLL(1)
    for (v = 0u; v < MP_NPITCH + 2u; v++) {
        if (v < MP_NOTE || !s)
            j->sh[v] = (uint8_t)v;
        else {
            d = mp_pitch_deg((int)v - (int)MP_NOTE, (int)j->key, &x);
            q = (int)j->key + mp_deg_semis(d + s) + x;
            j->sh[v] = (uint8_t)(q >= 0 && q < (int)MP_NPITCH ? MP_NOTE + (uint32_t)q : 0xFFu);
        }
    }
#pragma UNROLL(1)
    for (v = 0u; v <= MP_MAXN; v++) {
        j->row[0][v] = 0u;
        j->row[1][v] = 0u;
    }
    j->p = j->n - 1u;
}

/* One row of the current match pass: runs at p (d = 1..p) from the row of
 * p + 1; per p the longest run wins (strictly longer: smaller d first,
 * earlier pass first). */
MP_INLINE(mp_runs_row)
static inline void mp_runs_row(volatile MpJob *j)
{
    volatile MpTab *t = &j->tab[j->cur];
    uint32_t p = j->p, d, a = j->g[p], b, r, rh = j->pass >= 7u, m;
    volatile uint8_t *old = j->row[(p & 1u) ^ 1u], *nw = j->row[p & 1u];
    int s = rh ? 0 : mp_pass_shift(j->pass);
#pragma UNROLL(1)
    for (d = 1u; d <= p; d++) {
        b = j->g[p - d];
        m = rh ? ((a >= MP_NOTE) == (b >= MP_NOTE) && (a >= MP_NOTE || a == b)) : (a == j->sh[b]);
        if (m) {
            r = old[d] + 1u;
            nw[d] = (uint8_t)r;
            if (rh) {
                if (r > t->rmax[p]) {
                    t->rmax[p] = (uint8_t)r;
                    t->rsrc[p] = (uint8_t)d;
                }
            } else if (r > t->cmax[p]) {
                t->cmax[p] = (uint8_t)r;
                t->cdist[p] = (uint8_t)d;
                t->cshift[p] = (int8_t)s;
            }
        } else
            nw[d] = 0u;
    }
}

/* Relax state (q, lj) with cost c and move mv. */
MP_INLINE(mp_relax)
static inline void mp_relax(volatile MpJob *j, volatile MpTab *t, uint32_t q, uint32_t lj, uint32_t c, uint32_t mv)
{
    if (c < j->cost[q][lj]) {
        j->cost[q][lj] = (uint16_t)c;
        t->frm[q][lj] = mv;
    }
}

/* One DP slot p: every length index li with a cost, as the model's dp(). */
MP_INLINE(mp_dp_slot)
static inline void mp_dp_slot(volatile MpJob *j)
{
    volatile MpTab *t = &j->tab[j->cur];
    uint32_t n = j->n, p = j->p, li, lj, x, c, f, v, r, ln, q, dt = n <= 64u ? 2u : 3u;
    uint32_t cb = 3u * (4u + dt), rb = 3u * (3u + dt);
#pragma UNROLL(1)
    for (li = 0u; li < 8u; li++) {
        c = j->cost[p][li];
        if (c >= MP_INF) continue;
        if (p == n || t->rest_run[p] == n - p) {
            f = c + (p < n ? MP_C_ESC : 0u);
            if (f < t->best) {
                t->best = f;
                t->bp = p;
                t->bli = li;
            }
            if (p == n) continue;
        }
        v = j->g[p];
        if (v >= MP_NOTE) {
#pragma UNROLL(1)
            for (lj = 0u; lj < 8u; lj++)
                if (mp_t.len[lj] <= t->note_len[p])
                    mp_relax(j, t, p + mp_t.len[lj], lj, c + t->nc[p] + (lj != li ? MP_C_LEN : 0u),
                             MP_MV(MP_MV_N, p, li, 0u));
        } else if (v == MP_HOLD) {
#pragma UNROLL(1)
            for (x = 0u; x < 8u; x++)
                if (mp_t.len[x] <= t->hold_run[p])
                    mp_relax(j, t, p + mp_t.len[x], li, c + MP_C_EXT, MP_MV(MP_MV_X, p, li, x));
        } else {
            r = t->rest_run[p];
#pragma UNROLL(1)
            for (lj = 0u; lj < 8u; lj++)
                if (mp_t.len[lj] <= r)
                    mp_relax(j, t, p + mp_t.len[lj], lj, c + MP_C_ESC + (lj != li ? MP_C_LEN : 0u),
                             MP_MV(MP_MV_R, p, li, 0u));
            if (p > 0u && j->g[p - 1u] == MP_REST) {
#pragma UNROLL(1)
                for (x = 0u; x < 8u; x++)
                    if (mp_t.len[x] <= r)
                        mp_relax(j, t, p + mp_t.len[x], li, c + MP_C_EXT, MP_MV(MP_MV_X, p, li, x));
            }
        }
#pragma UNROLL(1)
        for (x = 0u; x < 8u; x++) {
            ln = mp_t.cp[x];
            q = p + ln < n ? p + ln : n;
            if (ln <= t->cmax[p] || (t->cmax[p] && t->cmax[p] == n - p))
                mp_relax(j, t, q, li, c + cb, MP_MV(MP_MV_C, p, li, x));
            if (ln <= t->rmax[p] || (t->rmax[p] && t->rmax[p] == n - p))
                mp_relax(j, t, q, li, c + rb + t->pre[q] - t->pre[p], MP_MV(MP_MV_Y, p, li, x));
        }
    }
}

/* Token writer for the emit: v at bit address *a (words zeroed before),
 * then the next address; *nt counts the tokens (written only while they fit
 * MP_MAXPARTS parts). */
MP_INLINE(mp_put)
static inline void mp_put(volatile MpJob *j, uint32_t *a, uint32_t *nt, uint32_t v)
{
    uint32_t b = *a;
    if (*nt < MP_NTOK * MP_MAXPARTS) {
        j->words[b >> 5] |= v << (b & 31u);
        if ((b & 31u) > 29u) j->words[(b >> 5) + 1u] |= v >> (32u - (b & 31u));
        *a = mp_next(b);
    }
    (*nt)++;
}

/* A note's tokens: JUMPs beyond +-3 degrees, SHARP, the note token. */
MP_INLINE(mp_note_tokens)
static inline void mp_note_tokens(volatile MpJob *j, uint32_t *a, uint32_t *nt, int d, int s, int cur)
{
    int i = d - cur, k;
#pragma UNROLL(1)
    while (i > 3) {
        k = i > 7 ? 7 : i;
        mp_put(j, a, nt, MP_ESC);
        mp_put(j, a, nt, MP_E_JUMP);
        mp_put(j, a, nt, (uint32_t)k);
        i -= k;
    }
#pragma UNROLL(1)
    while (i < -3) {
        k = i < -7 ? -7 : i;
        mp_put(j, a, nt, MP_ESC);
        mp_put(j, a, nt, MP_E_JUMP);
        mp_put(j, a, nt, (uint32_t)(k + 7));
        i -= k;
    }
    if (s) {
        mp_put(j, a, nt, MP_ESC);
        mp_put(j, a, nt, MP_E_SHARP);
    }
    mp_put(j, a, nt, (uint32_t)(i + 4));
}

/* The degree of the last note before slot q (none: the start degree). */
MP_INLINE(mp_last_deg)
static inline int mp_last_deg(volatile MpJob *j, uint32_t q, int key)
{
    int s;
#pragma UNROLL(1)
    for (; q > 0u; q--)
        if (j->g[q - 1u] >= MP_NOTE)
            return mp_pitch_deg((int)j->g[q - 1u] - (int)MP_NOTE, key, &s);
    return MP_START_DEG;
}

/* The best key's DP path -> tokens -> words; bits, parts (0 = more than
 * MP_MAXPARTS: words left zero), the key; err if the tokens' bits differ
 * from the DP's. */
MP_INLINE(mp_emit)
static inline void mp_emit(volatile MpJob *j)
{
    volatile MpTab *t = &j->tab[j->bidx];
    uint32_t n = j->n, key = t->key, bits = t->best, k = 0u, q, lj, mv, kind, p, li, x, i, dd;
    uint32_t a = MP_KEY_BITS, nt = 0u, dt = n <= 64u ? 2u : 3u;
    int cur = MP_START_DEG, d, s;
    j->okey = key;
    j->bits = bits;
    j->parts = 0u;
#pragma UNROLL(1)
    for (i = 1u; i <= MP_MAXPARTS; i++)
        if (bits <= MP_PART_BITS * i) {
            j->parts = i;
            break;
        }
#pragma UNROLL(1)
    for (i = 0u; i < 4u * MP_MAXPARTS; i++) j->words[i] = 0u;
    if (!j->parts) return;
    q = t->bp;
    lj = t->bli;
#pragma UNROLL(1)
    while ((mv = t->frm[q][lj]) != 0u) {        /* back from the end state */
        j->moves[k++] = mv | q << 17 | lj << 25;
        q = mv >> 3 & 0xFFu;
        lj = mv >> 11 & 7u;
    }
    j->words[0] = key;
    mp_put(j, &a, &nt, (n == 16u ? 0u : n == 32u ? 1u : n == 64u ? 2u : 3u) | j->step8 << 2);
#pragma UNROLL(1)
    while (k) {
        mv = j->moves[--k];
        kind = mv & 7u;
        p = mv >> 3 & 0xFFu;
        li = mv >> 11 & 7u;
        x = mv >> 14 & 7u;
        q = mv >> 17 & 0xFFu;
        lj = mv >> 25 & 7u;
        if ((kind == MP_MV_N || kind == MP_MV_R) && lj != li) {
            mp_put(j, &a, &nt, MP_ESC);
            mp_put(j, &a, &nt, MP_E_LEN);
            mp_put(j, &a, &nt, lj);
        }
        if (kind == MP_MV_N) {
            d = mp_pitch_deg((int)j->g[p] - (int)MP_NOTE, (int)key, &s);
            mp_note_tokens(j, &a, &nt, d, s, cur);
            cur = d;
        } else if (kind == MP_MV_R) {
            mp_put(j, &a, &nt, MP_ESC);
            mp_put(j, &a, &nt, MP_E_REST);
        } else if (kind == MP_MV_X) {
            mp_put(j, &a, &nt, MP_ESC);
            mp_put(j, &a, &nt, MP_E_EXT);
            mp_put(j, &a, &nt, x);
        } else if (kind == MP_MV_C) {
            mp_put(j, &a, &nt, MP_ESC);
            mp_put(j, &a, &nt, MP_E_COPY);
            dd = t->cdist[p] - 1u;
#pragma UNROLL(1)
            for (i = 0u; i < dt; i++) mp_put(j, &a, &nt, dd >> (3u * i) & 7u);
            mp_put(j, &a, &nt, x);
            mp_put(j, &a, &nt, (uint32_t)(t->cshift[p] + 3));
            cur = mp_last_deg(j, q, (int)key);
        } else {                                /* RHYTHM, then its notes */
            mp_put(j, &a, &nt, MP_ESC);
            mp_put(j, &a, &nt, MP_E_RHY);
            dd = t->rsrc[p] - 1u;
#pragma UNROLL(1)
            for (i = 0u; i < dt; i++) mp_put(j, &a, &nt, dd >> (3u * i) & 7u);
            mp_put(j, &a, &nt, x);
#pragma UNROLL(1)
            for (i = p; i < q; i++)
                if (j->g[i] >= MP_NOTE) {
                    d = mp_pitch_deg((int)j->g[i] - (int)MP_NOTE, (int)key, &s);
                    mp_note_tokens(j, &a, &nt, d, s, cur);
                    cur = d;
                }
        }
    }
    if (t->bp < n) {
        mp_put(j, &a, &nt, MP_ESC);
        mp_put(j, &a, &nt, MP_E_END);
    }
    if (3u * nt != bits) j->err = 1u;
}

/* Run the job for up to `units` units; returns 1 once done (result in
 * j->words / bits / parts / okey / err). */
MP_COLD(mp_run)
static uint32_t mp_run(MpJob *jp, uint32_t units)
{
    volatile MpJob *j = (volatile MpJob *)jp;
    volatile MpTab *t;
    uint32_t i, k;
#pragma UNROLL(1)
    for (; units; units--) {
        t = &j->tab[j->cur];
        if (j->phase == MP_PH_INIT) {
            mp_tables(j);
            j->pass = 0u;
            mp_pass_start(j);
            j->phase = MP_PH_RUNS;
        } else if (j->phase == MP_PH_RUNS) {
            mp_runs_row(j);
            if (--j->p == 0u) {                 /* pass done */
                if (++j->pass < MP_NPASS)
                    mp_pass_start(j);
                else {                          /* the DP, from slot 0 */
#pragma UNROLL(1)
                    for (i = 0u; i <= MP_MAXN; i++)
#pragma UNROLL(1)
                        for (k = 0u; k < 8u; k++) {
                            j->cost[i][k] = MP_INF;
                            t->frm[i][k] = 0u;
                        }
                    j->cost[0][MP_L_DEFAULT] = MP_C_SET;
                    t->best = 0xFFFFFFFFu;
                    t->bp = 0u;
                    t->bli = 0u;
                    j->p = 0u;
                    j->phase = MP_PH_DP;
                }
            }
        } else if (j->phase == MP_PH_DP) {
            mp_dp_slot(j);
            if (++j->p > j->n) {                /* this key done */
                if (t->best < j->bcost) {       /* strictly cheaper: keep its buffer */
                    j->bcost = t->best;
                    j->bidx = j->cur;
                    j->cur ^= 1u;
                }
                j->phase = ++j->key < 12u ? MP_PH_INIT : MP_PH_EMIT;
            }
        } else if (j->phase == MP_PH_EMIT) {
            mp_emit(j);
            j->phase = MP_PH_DONE;
        } else
            break;
    }
    return j->phase == MP_PH_DONE;
}

#endif /* MELOPACK_H */
