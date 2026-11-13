/* The one SongBox instrument engine: a wavetable voice driven by a record
 * (SsInst, from instruments/<id>.py via sets.py). Mirror: engine.py (the
 * sample formula is in its docstring). Included by songbox.c after
 * SsConst (SsInst: engine.h); needs SsVoice and inst_lib.h.
 *
 * ss_wt_build (out of line, .text): rows of every table from the term
 * lists ss_wt_terms (ends per table half in ss_wt_end); first the records
 * (SsState.inst) from ss_inst_pool / ss_inst_idx, and their block steps
 * (SsBlk). ss_run: a voice over a block of SS_BLK samples. The levels
 * (envelopes, LFO pitch) step once per block, exactly as SS_BLK samples of
 * the recurrences would, and ramp linearly inside it; then two loops (cl6x
 * at -ms3 only overlaps samples in loops with short chains): b = phases and
 * table reads -> sf; c = soft clip, DC blocker, block gain. */

/* ss_sin out of line: one copy for the table build and the LFO (neither
 * runs per sample) */
SS_COLD(ss_sin1)
static float ss_sin1(float p)
{
    return ss_sin(p);
}

/* term w of row i: amp x sin / cos of harmonic h (engine.py term_val) */
SS_COLD(ss_wt_term)
static float ss_wt_term(uint32_t i, uint32_t w)
{
    uint32_t ph = ((w & 0x3FFu) * i + ((w >> 10) & 1u) * (SS_WT_N / 4u)) & (SS_WT_N - 1u);
    return (float)((int)w >> 16) * ss_sin1((float)(int)ph * (1.0f / (float)SS_WT_N));
}

/* rows i .. i + n - 1 of every table (t: the tables one after the other;
 * c: SongBox's tables); at row 0 also the records rec[] from the pool and
 * their block steps bk[] */
SS_COLD(ss_wt_build)
static void ss_wt_build(const SsConst *c, uint32_t *t, uint32_t i, uint32_t n, uint32_t *rec, SsBlk *bk)
{
    uint32_t k, e, m, j;
    float a, b;
    if (i == 0u) {                      /* each record: the default, its mask's words from p */
        const SsInst *r = (const SsInst *)rec;
        const uint8_t *p = c->inst_idx + SS_INST_WORDS;
        #pragma UNROLL(1)
        for (k = 0u; k < SS_NREC; ++k) {
            m = c->inst_mask[k];
            #pragma UNROLL(1)
            for (j = 0u; j < SS_INST_WORDS; ++j, m >>= 1) {
                e = c->inst_idx[j];
                e += (*p - e) & (0u - (m & 1u));    /* the record's own word */
                p += m & 1u;
                *(volatile uint32_t *)rec++ = c->inst_pool[e];
            }
        }
        #pragma UNROLL(1)
        for (k = 0u; k < SS_NREC; ++k, ++r, ++bk) {
            float l = 1.0f, p = 1.0f, ma = 1.0f, rl = 1.0f, at = 1.0f, se = 0.0f, sm = 0.0f;
            #pragma UNROLL(1)
            for (j = 0u; j < SS_BLK; ++j) {
                l *= r->dec;                    /* dec ^ (j + 1) */
                p *= r->bdec;
                se = se * (1.0f - r->att) + l;  /* Horner: sum (1 - att)^(8-j) dec^j */
                sm = sm * r->ma + p;
                ma *= r->ma;
                rl *= r->rel;
                at *= 1.0f - r->att;
            }
            ((volatile SsBlk *)bk)->dec8 = l;
            ((volatile SsBlk *)bk)->bdec8 = p;
            ((volatile SsBlk *)bk)->ma8 = ma;
            ((volatile SsBlk *)bk)->rel8 = rl;
            ((volatile SsBlk *)bk)->at8 = at;
            ((volatile SsBlk *)bk)->se = se * r->att;
            ((volatile SsBlk *)bk)->sm = sm * r->mb;
        }
    }
    for (; n; --n, ++i) {               /* the halves in turn: low (a), high (b) */
        e = 0u;
        a = 0.0f;
        #pragma UNROLL(1)
        for (k = 0u; k < 2u * SS_NTAB; ++k) {
            b = 0.0f;
            for (; e < c->wt_end[k]; ++e) b += ss_wt_term(i, c->wt_terms[e]);
            if (k & 1u)
                ((volatile uint32_t *)t)[(k >> 1) * SS_WT_ROWS + i] =
                    (((uint32_t)(int)(a + 32768.5f) - 32768u) & 0xFFFFu)
                    | (uint32_t)(int)(b + (SS_WT_OFS + 0.5f)) << 16;
            a = b;
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

/* voice v by instrument r (block steps bk): a new note (v->trig) first,
 * then SS_BLK samples x g added to acc[], the gate counting down by ginc
 * per sample (t: r's table; scratch sf[SS_BLK]). The voice is copied to w
 * (its fields stay in registers), the record's fields to locals. Per
 * sample (engine.py): ip / ip2 by the block's steps; the three read levels
 * from the block's start to its end values (as the per-sample recurrences
 * give them at its last sample). */
ZM_INLINE(ss_run)
static inline void ss_run(SsVoice *v, const SsInst *restrict r, const SsBlk *restrict bk, float g,
                          float ginc, float *restrict acc, const uint32_t *restrict t, float *restrict sf)
{
    SsVoice w;
    uint32_t i, j, j2, j3, d1, d2, sh = r->sh;
    float k, tf, f, f2, oa, ob, ca, cb, x, c, y, a0, a1, a2, e0, e1, e2;
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
    tf = (float)_abs((int)(w.ip3 >> 1) - 0x40000000);  /* triangle 0..2^30 */
    w.ip3 += r->rot * SS_BLK;
    d1 = (uint32_t)(int)(w.inc * (r->pa + r->pb * tf));
    d2 = (uint32_t)(int)(w.inc * (r->pc + r->pb * tf));
    a0 = w.env * (r->g0 + r->g1 * w.menv);             /* the read levels now */
    a1 = a0 * (r->r2 + r->q2 * w.menv + r->b2 * w.bp);
    a2 = a0 * r->b3 * w.bp;
    k = (float)(int)(w.gate > 0.0f);
    w.env = w.env * (bk->rel8 + k * (bk->at8 - bk->rel8)) + k * bk->se * w.lp2;
    w.lp2 *= bk->dec8;
    w.menv = w.menv * bk->ma8 + bk->sm * w.bp;
    w.bp *= bk->bdec8;
    w.gate -= ginc * (float)SS_BLK;
    e0 = w.env * (r->g0 + r->g1 * w.menv);             /* ... at the block's end */
    e1 = e0 * (r->r2 + r->q2 * w.menv + r->b2 * w.bp);
    e2 = e0 * r->b3 * w.bp;
    e0 = (e0 - a0) * (1.0f / (float)SS_BLK);           /* steps per sample */
    e1 = (e1 - a1) * (1.0f / (float)SS_BLK);
    e2 = (e2 - a2) * (1.0f / (float)SS_BLK);
    for (i = 0u; i < SS_BLK; ++i) {
        w.ip += d1;
        w.ip2 += d2;
        a0 += e0;
        a1 += e1;
        a2 += e2;
        j = w.ip >> (32u - SS_WT_BITS);                 /* low half at ip, linear */
        f = (float)(int)(w.ip & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
        j2 = w.ip2 >> (32u - SS_WT_BITS);               /* high half at ip2, linear */
        f2 = (float)(int)(w.ip2 & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
        j3 = (w.ip << sh) >> (32u - SS_WT_BITS);        /* high half at ip << sh, nearest */
        oa = (float)((int)(t[j] << 16) >> 16);
        ob = (float)((int)(t[j + 1u] << 16) >> 16);
        ca = (float)(int)(t[j2] >> 16);
        cb = (float)(int)(t[j2 + 1u] >> 16);
        sf[i] = a0 * (oa + (ob - oa) * f)
              + a1 * (ca + (cb - ca) * f2 - SS_WT_OFS)
              + a2 * ((float)(int)(t[j3] >> 16) - SS_WT_OFS);
    }
    for (i = 0u; i < SS_BLK; ++i) {
        x = sf[i];
        c = _fabsf(x + 1.0f) - _fabsf(x - 1.0f);        /* 2 clamp1(x) */
        y = 0.75f * c + c * c * (e - 0.0625f * c);      /* soft clip (+ even part) */
        acc[i] += (y - w.lp) * w.p3;
        w.lp = w.lp * la + y * dc;                      /* DC blocker */
    }
    SS_VCOPY(*v, w);
}
