/* songpack: song bar list <-> Floppy token stream (format: songseq.py).
 *
 * Line-by-line mirror of songpack.py (the C-style model; songseq.py is the
 * reference). Any change goes into both files; songpack_vectors.txt and the
 * SongBox self-test check them against each other.
 *
 * Bar = one byte: chord (root * 2 + minor, 0..23; SP_REST = 24; SP_DRUMS =
 * 26: no chord, the drums play its rhythm, stream FLAT + 7) | rhythm << 5.
 * Stream: key (4 bits), then 3-bit tokens, LSB first, in 4 words (106 bits).
 * More Floppies (parts 1..SP_MAX_PARTS): the stream goes on, 34 tokens per
 * part; part q is words[4 q .. 4 q + 3], its first 4 bits the key (part 1)
 * or the part marker (extra copies, zmenu's; skipped here).
 * Settings (SongBox MENU OCTAVE + LENGTH, a code 0..8; 0 = defaults) first
 * in the stream: code 0..6 one token, 7 / 8 token 7 + 0 / 1 (7 + 2..7 read
 * as 0, free for later).
 *
 * Packer: sp_start(), then sp_step() until it returns 1 (one prep row or one
 * DP row per step, <= ~240 inner operations; ~1,800 steps for 128 bars).
 * Result: fits, bits, bkey, words[4 * parts]. Unpacker: sp_unpack().
 *
 * Rules: force-inlined helpers, no division / modulo, no stack arrays; all
 * state in SpPack (ctx[3]). */
#ifndef SONGPACK_H
#define SONGPACK_H
#include <stdint.h>

#define SP_DO_PRAGMA(x) _Pragma(#x)
#define SP_EXPAND_PRAGMA(x) SP_DO_PRAGMA(x)
#define SP_INLINE(f) SP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(f))
/* The packer step and the small helpers used at many sites (sp_put,
 * sp_get_bits3, sp_append, sp_next_addr, sp_mod12) may be kept out of line
 * (one copy each, called): define SP_COLD(f) before including songpack.h;
 * inlined by default. sp_relax stays inline (sp_dp_row's copy loop). */
#ifndef SP_COLD
#define SP_COLD(f) SP_INLINE(f)
#endif

#define SP_CAP_BITS   106u
#define SP_KEY_BITS   4u
#define SP_NTOK       34u               /* (106 - 4) / 3 tokens per part (Floppy) */
#define SP_BUDGET     102u              /* token bits per part */
#define SP_MAX_PARTS  4u                /* G1on chain of 5 = SongBox + 4 Floppies */
#define SP_NWORDS     (4u * SP_MAX_PARTS)
#define SP_MAX_BARS   128u
#define SP_COPY_MAX_N 64u
#define SP_INF        0xFFFFu
#define SP_REST       24u
#define SP_DRUMS      26u               /* 25: SongBox's END (never packed) */
/* a bar byte's chord part has a rhythm: a chord or a drums bar */
#define SP_HAS_RHY(c) ((c) < SP_REST || (c) == SP_DRUMS)

#define SP_EV      0u
#define SP_E_END   0u
#define SP_E_SWITCH 1u
#define SP_E_FLAT  2u
#define SP_E_NEXT  3u
#define SP_E_PREV  4u
#define SP_E_REP3  5u
#define SP_E_COPY  6u
#define SP_E_SECT  7u
#define SP_REST_TOK 4u
#define SP_DRUMS_TOK 7u                 /* FLAT + 7 = drums bar */
#define SP_DRUMS_CODE 15u               /* sp_rel_code form of a drums bar: FLAT, token 7 */
#define SP_SET_ESC  7u                  /* settings token 7: escape, one more token follows */
#define SP_SET_N    9u                  /* settings codes 0..8 */

#define SP_BITS_EV   6u
#define SP_BITS_REP3 6u
#define SP_BITS_SECT 9u
#define SP_BITS_COPY 15u
#define SP_BITS_REST 9u

#define SP_M_BAR  1u                    /* op[] = type << 8 | arg */
#define SP_M_REP3 2u
#define SP_M_SECT 3u
#define SP_M_COPY 4u

#define SP_END_DONE 0u                  /* sp_unpack() end: END, zero tail, 128 bars */
#define SP_END_OUT  1u                  /* tokens ran out between two items */
#define SP_END_CUT  2u                  /* ran out inside an event */

/* rel_code: chord relative to the key -> degree token | flat << 3 | default
 * minor << 4. One byte struct: no 8-byte alignment (and padding) as each
 * char array gets. */
static const struct { uint8_t rel_code[12], deg_ofs[7], deg_min[7]; } sp_tabs = {
    { 1u, 10u, 18u, 11u, 19u, 4u, 13u, 5u, 14u, 22u, 7u, 9u },
    { 0u, 2u, 4u, 5u, 7u, 9u, 10u },
    { 0u, 1u, 1u, 0u, 0u, 1u, 0u } };
#define sp_rel_code (sp_tabs.rel_code)
#define sp_deg_ofs  (sp_tabs.deg_ofs)
#define sp_deg_min  (sp_tabs.deg_min)

typedef struct {
    uint32_t n, phase, p, k, cur, best, bkey, bcost, ntok, bits, fits;
    uint32_t parts, budget;             /* Floppies packed for, their token bits */
    uint32_t pre;                       /* settings code 0..8 */
    uint32_t words[SP_NWORDS];
    uint16_t cost[2][SP_MAX_BARS + 1u];
    uint16_t op[2][SP_MAX_BARS + 1u];
    uint8_t frm[2][SP_MAX_BARS + 1u];
    uint8_t bars[SP_MAX_BARS];
    uint8_t rh[SP_MAX_BARS];            /* rhythm state after bar i */
    uint8_t sect[SP_MAX_BARS * 3u];     /* SECT r at p: section length, 0 = none */
    uint8_t copy[SP_MAX_BARS * SP_MAX_BARS];  /* [p * 128 + j - p - 1] = N, 0 = none */
    uint8_t run_n[SP_COPY_MAX_N + 1u];
    uint8_t sec_s[3], sec_e[3];
    uint8_t tok[SP_NTOK * SP_MAX_PARTS];
    uint8_t stack[SP_MAX_BARS + 1u];
} SpPack;

SP_COLD(sp_mod12)
static inline uint32_t sp_mod12(int x)
{
    if (x < 0) x += 12;
    if (x >= 12) x -= 12;
    return (uint32_t)x;
}

/* Bit address of the token after the one at a (parts 128 bits apart,
 * tokens in bits 4..105 of each). */
SP_COLD(sp_next_addr)
static inline uint32_t sp_next_addr(uint32_t a)
{
    a += 3u;
    if ((a & 127u) == 106u) a += 26u;
    return a;
}

/* Self-test check value of w[0 .. 4 * parts). */
SP_INLINE(sp_words_hash)
static inline uint32_t sp_words_hash(const uint32_t *w, uint32_t parts)
{
    uint32_t h = 0u, i;
    for (i = 0u; i < 4u * parts; ++i) h = h * 31u + w[i];
    return h;
}

SP_INLINE(sp_bar_bits)
static inline uint32_t sp_bar_bits(uint32_t key, uint32_t b, uint32_t beat)
{
    uint32_t ch = b & 31u, bits, code;
    if (ch == SP_REST) return SP_BITS_REST;
    bits = 3u;
    if ((b >> 5) != beat) bits += SP_BITS_EV;
    code = ch == SP_DRUMS ? SP_DRUMS_CODE : sp_rel_code[sp_mod12((int)(ch >> 1) - (int)key)];
    if (code & 8u) bits += SP_BITS_EV;
    if ((ch & 1u) != (code >> 4)) bits += SP_BITS_EV;
    return bits;
}

/* ------------------------------------------------------------- driver */

SP_INLINE(sp_start)
static inline void sp_start(SpPack *P, const uint8_t *bars, uint32_t n, uint32_t parts, uint32_t pre)
{
    uint32_t i;
    for (i = 0u; i < n; ++i) P->bars[i] = bars[i];
    P->n = n;
    P->parts = parts;
    P->pre = pre;
    P->budget = SP_BUDGET * parts;      /* token bits of all parts */
    P->phase = 0u;                      /* 0 copy rows (backward), 1 rhythm / SECT rows, */
    P->p = n - 1u;                      /* 2 key DP, 3 done; p wraps to ~0 when n = 0 */
    P->k = 0u;
    P->cur = 0u;
    P->best = 0u;
    P->bkey = 0u;
    P->bcost = SP_INF;
    P->fits = 0u;
    for (i = 0u; i < 3u; ++i) { P->sec_s[i] = 0u; P->sec_e[i] = 0u; }
    for (i = 0u; i <= SP_COPY_MAX_N; ++i) P->run_n[i] = 0u;
}

/* ------------------------------------------------------------- prep (key-free) */

/* Rows from the end: run_n[N] = bars from p on equal to the bar N before. */
SP_INLINE(sp_copy_row)
static inline void sp_copy_row(SpPack *P, uint32_t p)
{
    uint8_t *row = &P->copy[p * SP_MAX_BARS];
    uint32_t i, N, top, L, lim, t;
    for (i = 0u; i < SP_MAX_BARS; ++i) row[i] = 0u;
    top = p < SP_COPY_MAX_N ? p : SP_COPY_MAX_N;
    for (N = 1u; N <= top; ++N) {
        if (P->bars[p] == P->bars[p - N]) P->run_n[N]++;
        else P->run_n[N] = 0u;
        L = P->run_n[N];
        lim = N << 3;
        if (L > lim) L = lim;
        for (t = N; t <= L; t += N)     /* every whole repeat is a target */
            if (row[t - 1u] == 0u) row[t - 1u] = (uint8_t)N;
    }
}

SP_INLINE(sp_sect_row)
static inline void sp_sect_row(SpPack *P, uint32_t p)
{
    uint32_t r, s, e, ln, i, x, b;
    if (p > 0u) {                       /* extend / start the section of bar p - 1 */
        x = P->rh[p - 1u];
        if (p > 1u && P->rh[p - 2u] == x) P->sec_e[x] = (uint8_t)p;
        else { P->sec_s[x] = (uint8_t)(p - 1u); P->sec_e[x] = (uint8_t)p; }
    }
    for (r = 0u; r < 3u; ++r) {         /* SECT r: next bars equal the section? */
        s = P->sec_s[r]; e = P->sec_e[r];
        ln = 0u;
        if (e > 0u && p + (e - s) <= P->n) {
            ln = e - s;
            for (i = 0u; i < e - s; ++i)
                if (P->bars[p + i] != P->bars[s + i]) { ln = 0u; break; }
        }
        P->sect[p * 3u + r] = (uint8_t)ln;
    }
    b = P->bars[p];                     /* rhythm state after bar p */
    if ((b & 31u) == SP_REST) P->rh[p] = p > 0u ? P->rh[p - 1u] : 0u;
    else P->rh[p] = (uint8_t)(b >> 5);
}

/* ------------------------------------------------------------- DP per key */

SP_INLINE(sp_key_begin)
static inline void sp_key_begin(SpPack *P)
{
    uint16_t *c = P->cost[P->cur];
    uint32_t j;
    for (j = 0u; j <= P->n; ++j) c[j] = SP_INF;
    c[0] = P->pre < SP_SET_ESC ? 3u : 6u;     /* settings tokens */
    P->p = 0u;
}

SP_INLINE(sp_relax)
static inline void sp_relax(SpPack *P, uint32_t j, uint32_t c, uint32_t p, uint32_t op)
{
    uint16_t *cost = P->cost[P->cur];
    if (c <= P->budget && c < cost[j]) {
        cost[j] = (uint16_t)c;
        P->frm[P->cur][j] = (uint8_t)p;
        P->op[P->cur][j] = (uint16_t)op;
    }
}

SP_INLINE(sp_dp_row)
static inline void sp_dp_row(SpPack *P, uint32_t p)
{
    uint32_t c0 = P->cost[P->cur][p], beat, r, ln, i, N;
    const uint8_t *row;
    if (c0 == SP_INF) return;
    beat = p > 0u ? P->rh[p - 1u] : 0u;
    sp_relax(P, p + 1u, c0 + sp_bar_bits(P->k, P->bars[p], beat), p, SP_M_BAR << 8);
    if (p > 0u && p + 3u <= P->n && P->copy[p * SP_MAX_BARS + 2u] == 1u)
        /* copy table offset 3 holds N = 1 iff bars[p..p+2] == bars[p-1] */
        sp_relax(P, p + 3u, c0 + SP_BITS_REP3, p, SP_M_REP3 << 8);
    for (r = 0u; r < 3u; ++r) {
        ln = P->sect[p * 3u + r];
        if (ln) sp_relax(P, p + ln, c0 + SP_BITS_SECT, p, SP_M_SECT << 8 | r);
    }
    row = &P->copy[p * SP_MAX_BARS];
    for (i = 0u; i < P->n - p; ++i) {
        N = row[i];
        if (N) sp_relax(P, p + i + 1u, c0 + SP_BITS_COPY, p, SP_M_COPY << 8 | N);
    }
}

SP_INLINE(sp_key_end)
static inline void sp_key_end(SpPack *P)
{
    uint32_t c = P->cost[P->cur][P->n];
    if (c < P->bcost) {                 /* keep this key's arrays, DP the next in the other */
        P->bcost = c;
        P->bkey = P->k;
        P->best = P->cur;
        P->cur ^= 1u;
    }
}

/* ------------------------------------------------------------- tokens */

SP_COLD(sp_put)
static inline void sp_put(SpPack *P, uint32_t t)
{
    P->tok[P->ntok++] = (uint8_t)t;
}

SP_INLINE(sp_put_bits)
static inline void sp_put_bits(uint32_t *w, uint32_t pos, uint32_t v)
{
    uint32_t sh = pos & 31u;
    w[pos >> 5] |= v << sh;
    if (sh > 28u) w[(pos >> 5) + 1u] |= v >> (32u - sh);
}

SP_INLINE(sp_emit)
static inline void sp_emit(SpPack *P)
{
    const uint8_t *frm;
    const uint16_t *op;
    uint32_t sp = 0u, j, p, o, t, b, ch, beat, code, N, c, s, i, a;
    int d;
    P->fits = P->bcost != SP_INF;
    P->ntok = 0u;
    for (i = 0u; i < SP_NWORDS; ++i) P->words[i] = 0u;
    if (!P->fits) { P->bits = SP_INF; return; }
    if (P->pre < SP_SET_ESC) {          /* settings first */
        sp_put(P, P->pre);
    } else {
        sp_put(P, SP_SET_ESC); sp_put(P, P->pre - SP_SET_ESC);
    }
    frm = P->frm[P->best];
    op = P->op[P->best];
    for (j = P->n; j > 0u; j = frm[j])  /* back pointers -> positions, last first */
        P->stack[sp++] = (uint8_t)j;
    while (sp > 0u) {
        j = P->stack[--sp];
        p = frm[j];
        o = op[j];
        t = o >> 8;
        if (t == SP_M_BAR) {
            b = P->bars[p];
            ch = b & 31u;
            if (ch == SP_REST) {
                sp_put(P, SP_EV); sp_put(P, SP_E_FLAT); sp_put(P, SP_REST_TOK);
            } else {
                beat = p > 0u ? P->rh[p - 1u] : 0u;
                d = (int)(b >> 5) - (int)beat;
                if (d < 0) d += 3;
                if (d == 1) { sp_put(P, SP_EV); sp_put(P, SP_E_NEXT); }
                else if (d == 2) { sp_put(P, SP_EV); sp_put(P, SP_E_PREV); }
                code = ch == SP_DRUMS ? SP_DRUMS_CODE : sp_rel_code[sp_mod12((int)(ch >> 1) - (int)P->bkey)];
                if (code & 8u) { sp_put(P, SP_EV); sp_put(P, SP_E_FLAT); }
                if ((ch & 1u) != (code >> 4)) { sp_put(P, SP_EV); sp_put(P, SP_E_SWITCH); }
                sp_put(P, code & 7u);
            }
        } else if (t == SP_M_REP3) {
            sp_put(P, SP_EV); sp_put(P, SP_E_REP3);
        } else if (t == SP_M_SECT) {
            sp_put(P, SP_EV); sp_put(P, SP_E_SECT); sp_put(P, o & 7u);
        } else {
            N = o & 255u;
            c = 0u;
            for (s = 0u; s < j - p; s += N) c++;    /* count = (j - p) / N */
            sp_put(P, SP_EV); sp_put(P, SP_E_COPY);
            sp_put(P, (N - 1u) & 7u); sp_put(P, (N - 1u) >> 3); sp_put(P, c - 1u);
        }
    }
    P->bits = SP_KEY_BITS + 3u * P->ntok;
    sp_put_bits(P->words, 0u, P->bkey);
    a = SP_KEY_BITS;
    for (i = 0u; i < P->ntok; ++i) {
        sp_put_bits(P->words, a, P->tok[i]);
        a = sp_next_addr(a);
    }
}

/* One step; returns 1 when done (result in fits / bits / bkey / words). */
SP_COLD(sp_step)
static inline uint32_t sp_step(SpPack *P)
{
    if (P->phase == 0u) {
        if (P->p != 0xFFFFFFFFu) {      /* p counts down to 0, then wraps */
            sp_copy_row(P, P->p);
            P->p--;
        } else {
            P->phase = 1u;
            P->p = 0u;
        }
    } else if (P->phase == 1u) {
        if (P->p < P->n) {
            sp_sect_row(P, P->p);
            P->p++;
        } else {
            P->phase = 2u;
            sp_key_begin(P);
        }
    } else if (P->phase == 2u) {
        if (P->p < P->n) {
            sp_dp_row(P, P->p);
            P->p++;
        } else {
            sp_key_end(P);
            P->k++;
            if (P->k < 12u) sp_key_begin(P);
            else { P->phase = 3u; sp_emit(P); }
        }
    }
    return P->phase == 3u;
}

/* ------------------------------------------------------------- unpacker */

SP_COLD(sp_get_bits3)
static inline uint32_t sp_get_bits3(const uint32_t *w, uint32_t pos)
{
    uint32_t sh = pos & 31u, v = w[pos >> 5] >> sh;
    if (sh > 29u) v |= w[(pos >> 5) + 1u] << (32u - sh);
    return v & 7u;
}

SP_COLD(sp_append)
static inline uint32_t sp_append(uint8_t *out, uint8_t *rh, uint32_t n, uint32_t b)
{
    out[n] = (uint8_t)b;
    if ((b & 31u) == SP_REST) rh[n] = n > 0u ? rh[n - 1u] : 0u;
    else rh[n] = (uint8_t)(b >> 5);
    return n + 1u;
}

/* Next token (at bit address a, the pc-th): a and pc move on. */
#define SP_TOK(t) do { (t) = sp_get_bits3(w, a); a = sp_next_addr(a); pc++; } while (0)

/* Floppy words (106 bits in each of `parts` 4-word parts) -> bars in out[],
 * rhythm states in rh[]; returns the bar count | SP_END_* << 8 | settings
 * code << 16. Mirrors
 * songpack.unpack_end() / songseq.unpack(), junk included. */
SP_INLINE(sp_unpack)
static inline uint32_t sp_unpack(const uint32_t *w, uint8_t *out, uint8_t *rh, uint32_t parts)
{
    uint32_t key = w[0] & 15u, n = 0u, pc = 0u, beat = 0u, flat = 0u, sw = 0u, pend = 0u;
    uint32_t ntok = SP_NTOK * parts, a = SP_KEY_BITS, fin = SP_END_OUT, set;
    uint32_t t, e, r, d, root, minor, end, s, q, N, c, total, k;
    int i;
    if (key > 11u) key = 0u;
    SP_TOK(set);                        /* settings: one token, or 7 + one more */
    if (set == SP_SET_ESC) {
        SP_TOK(e);
        set = e < SP_SET_N - SP_SET_ESC ? SP_SET_ESC + e : 0u;
    }
    while (n < SP_MAX_BARS && pc < ntok) {
        SP_TOK(t);
        if (t != SP_EV) {
            if (flat && t == SP_REST_TOK) {
                n = sp_append(out, rh, n, SP_REST);
            } else if (flat && t == SP_DRUMS_TOK) {
                n = sp_append(out, rh, n, SP_DRUMS | beat << 5);
            } else {
                d = t - 1u;
                root = sp_mod12((int)(key + sp_deg_ofs[d]) - (int)flat);
                minor = (flat ? 0u : sp_deg_min[d]) ^ sw;
                n = sp_append(out, rh, n, (root << 1 | minor) | beat << 5);
            }
            flat = 0u; sw = 0u; pend = 0u;
            continue;
        }
        if (pc >= ntok) break;
        SP_TOK(e);
        if (e == SP_E_END) { fin = SP_END_DONE; break; }
        pend = 1u;                      /* an event: until the next bar or block */
        if (e == SP_E_SECT) {
            if (pc >= ntok) { fin = SP_END_CUT; break; }
            SP_TOK(r);
            i = (int)n - 1;             /* last section of rhythm r in out[:n] */
            while (i >= 0 && rh[i] != r) i--;
            if (i >= 0) {
                end = (uint32_t)i + 1u;
                while (i >= 0 && rh[i] == r) i--;
                s = (uint32_t)(i + 1);
                for (q = s; q < end && n < SP_MAX_BARS; ++q) n = sp_append(out, rh, n, out[q]);
                beat = rh[n - 1u];
            }
            pend = 0u;
        } else if (e == SP_E_SWITCH) {
            sw ^= 1u;
        } else if (e == SP_E_FLAT) {
            flat = 1u;
        } else if (e == SP_E_NEXT) {
            beat = beat < 2u ? beat + 1u : 0u;
        } else if (e == SP_E_PREV) {
            beat = beat > 0u ? beat - 1u : 2u;
        } else if (e == SP_E_COPY) {
            if (pc + 3u > ntok) { fin = SP_END_CUT; break; }
            SP_TOK(N);
            SP_TOK(c);
            N |= c << 3;
            N += 1u;
            SP_TOK(c);
            if (N <= n) {
                total = N;
                for (k = 0u; k < c; ++k) total += N;
                for (k = 0u; k < total && n < SP_MAX_BARS; ++k) n = sp_append(out, rh, n, out[n - N]);
                beat = rh[n - 1u];
            }
            pend = 0u;
        } else if (e == SP_E_REP3) {
            if (n > 0u)
                for (k = 0u; k < 3u && n < SP_MAX_BARS; ++k) n = sp_append(out, rh, n, out[n - 1u]);
            pend = 0u;
        }
    }
    if (n >= SP_MAX_BARS) fin = SP_END_DONE;
    else if (fin == SP_END_OUT && pend) fin = SP_END_CUT;   /* after FLAT / SWITCH / NEXT / PREV */
    return n | fin << 8 | set << 16;
}
#undef SP_TOK

#endif /* SONGPACK_H */
