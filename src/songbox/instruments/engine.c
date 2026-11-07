/* The one SongBox instrument engine: a wavetable voice driven by a record
 * (SsInst, from instruments/<id>.py via sets.py). Mirror: engine.py (the
 * sample formula is in its docstring). Included by songbox.c after
 * SsConst (SsInst: engine.h); needs SsVoice and inst_lib.h.
 *
 * ss_wt_build (out of line, .text): rows of every instrument's table from
 * the term lists ss_wt_terms (ends per table half in ss_wt_end). ss_run: a
 * voice over a piece, as three loops (cl6x at -ms3 only overlaps samples in
 * loops with short chains): a = what is carried from sample to sample ->
 * si[2] phases, sf[3] levels; b = the table reads -> sf[1]; c = soft clip,
 * DC blocker, piece gain. */

/* ss_sin out of line: one copy for the table build and the LFO (neither
 * runs per sample) */
SS_COLD(ss_sin1)
static float ss_sin1(float p)
{
    return ss_sin(p);
}

/* term w of row i: amp x sin / cos of harmonic h (engine.py term_val) */
ZM_INLINE(ss_wt_term)
static inline float ss_wt_term(uint32_t i, uint32_t w)
{
    uint32_t ph = ((w & 0x3FFu) * i + ((w >> 10) & 1u) * (SS_WT_N / 4u)) & (SS_WT_N - 1u);
    return (float)((int)w >> 16) * ss_sin1((float)(int)ph * (1.0f / (float)SS_WT_N));
}

/* rows i .. i + n - 1 of every table (t: the tables one after the other;
 * c: SongBox's tables) */
SS_COLD(ss_wt_build)
static void ss_wt_build(const SsConst *c, uint32_t *t, uint32_t i, uint32_t n)
{
    uint32_t k, e;
    float a, b;
    for (; n; --n, ++i) {
        e = 0u;
        #pragma UNROLL(1)
        for (k = 0u; k < SS_NINST; ++k) {
            a = 0.0f;
            for (; e < c->wt_end[2u * k]; ++e) a += ss_wt_term(i, c->wt_terms[e]);
            b = 0.0f;
            for (; e < c->wt_end[2u * k + 1u]; ++e) b += ss_wt_term(i, c->wt_terms[e]);
            ((volatile uint32_t *)t)[k * SS_WT_ROWS + i] =
                (((uint32_t)(int)(a + 32768.5f) - 32768u) & 0xFFFFu)
                | (uint32_t)(int)(b + (SS_WT_OFS + 0.5f)) << 16;
        }
    }
}

/* the piece gain at LFO phase u (without the voice level) */
SS_COLD(ss_lfo)
static float ss_lfo(uint32_t u, const SsInst *r)
{
    return (r->t0 + r->ts * ss_sin1((float)(int)(u >> 8) * (1.0f / 16777216.0f))
            + r->tt * (float)_abs((int)(u >> 1) - 0x40000000) * (1.0f / 1073741824.0f)) * r->post;
}

/* voice v by instrument r: a new note (v->trig) first, then n samples x g
 * added to acc[0..n), the gate counting down by ginc per sample (t: r's
 * table; scratch si[2n] / sf[3n]). The voice is copied to w for the loops
 * (its fields stay in registers), the record's fields to locals. */
ZM_INLINE(ss_run)
static inline void ss_run(SsVoice *v, const SsInst *restrict r, float g, float ginc,
                          float *restrict acc, uint32_t n, const uint32_t *restrict t,
                          uint32_t *restrict si, float *restrict sf)
{
    SsVoice w;
    uint32_t i, j, j2, j3, ip, ip2, rot = r->rot, sh = r->sh;
    float k, tf, s0, f, f2, oa, ob, ca, cb, x, c, y;
    float pa = r->pa, pb = r->pb, pc = r->pc, att = r->att, dec = r->dec, rel = r->rel;
    float ar = 1.0f - r->att - r->rel, bdec = r->bdec, ma = r->ma, mb = r->mb;
    float g0 = r->g0, g1 = r->g1, r2 = r->r2, q2 = r->q2, b2 = r->b2, b3 = r->b3;
    float e = r->e, la = r->la, dc = r->dc;
    SS_VCOPY(w, *v);
    if (w.trig) {
        w.trig = 0u;
        w.lp2 = 1.0f;
        w.bp = r->bp0;
        w.menv = w.menv * r->mk + r->m0;
        w.ip2 = w.ip2 * r->r0 + w.ip * r->m2;
    }
    w.p3 = ss_lfo(w.ip3, r) * g;
    for (i = 0u; i < n; ++i) {
        w.ip3 += rot;
        tf = (float)_abs((int)(w.ip3 >> 1) - 0x40000000);  /* triangle 0..2^30 */
        w.ip += (uint32_t)(int)(w.inc * (pa + pb * tf));
        w.ip2 += (uint32_t)(int)(w.inc * (pc + pb * tf));
        k = (float)(int)(w.gate > 0.0f);
        w.lp2 *= dec;
        w.env = w.env * (rel + k * ar) + k * att * w.lp2;
        w.bp *= bdec;
        w.menv = w.menv * ma + w.bp * mb;
        si[2u * i] = w.ip;
        si[2u * i + 1u] = w.ip2;
        s0 = w.env * (g0 + g1 * w.menv);
        sf[3u * i] = s0;
        sf[3u * i + 1u] = s0 * (r2 + q2 * w.menv + b2 * w.bp);
        sf[3u * i + 2u] = s0 * b3 * w.bp;
        w.gate -= ginc;
    }
    for (i = 0u; i < n; ++i) {
        ip = si[2u * i];
        ip2 = si[2u * i + 1u];
        j = ip >> (32u - SS_WT_BITS);                   /* low half at ip, linear */
        f = (float)(int)(ip & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
        j2 = ip2 >> (32u - SS_WT_BITS);                 /* high half at ip2, linear */
        f2 = (float)(int)(ip2 & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
        j3 = (ip << sh) >> (32u - SS_WT_BITS);          /* high half at ip << sh, nearest */
        oa = (float)((int)(t[j] << 16) >> 16);
        ob = (float)((int)(t[j + 1u] << 16) >> 16);
        ca = (float)(int)(t[j2] >> 16);
        cb = (float)(int)(t[j2 + 1u] >> 16);
        sf[3u * i + 1u] = sf[3u * i] * (oa + (ob - oa) * f)
                        + sf[3u * i + 1u] * (ca + (cb - ca) * f2 - SS_WT_OFS)
                        + sf[3u * i + 2u] * ((float)(int)(t[j3] >> 16) - SS_WT_OFS);
    }
    for (i = 0u; i < n; ++i) {
        x = sf[3u * i + 1u];
        c = _fabsf(x + 1.0f) - _fabsf(x - 1.0f);        /* 2 clamp1(x) */
        y = 0.75f * c + c * c * (e - 0.0625f * c);      /* soft clip (+ even part) */
        acc[i] += (y - w.lp) * w.p3;
        w.lp = w.lp * la + y * dc;                      /* DC blocker */
    }
    SS_VCOPY(*v, w);
}
