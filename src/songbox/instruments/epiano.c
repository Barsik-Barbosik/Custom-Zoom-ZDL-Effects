/* EPiano (keys), shimmering Wurlitzer-style reed: two reeds a few cents apart,
 * soft clip harder at the strike, DC blocked, fast tremolo; also its set's
 * bass (see epiano.py). Constants SS_I_EPIANO_*; mirror: epiano.py. ss_t_:
 * one wavetable word, ss_h_: a new note (v->trig), ss_p_: once per piece
 * (tremolo), ss_ia_ / ss_ib_ / ss_ic_: one sample, reading the wavetable t (SS_WT_N + 1 words). Envelopes: env
 * rises to the slow decay lp2, the strike menv rises to bp (no jumps). */

/* Wavetable word for s = ss_sin(i / SS_WT_N) (shared by all tables' row i):
 * low half reed 1 + its 2nd harmonic (h2 = cos 2x = 1 - 2 s^2)
 * x SS_I_EPIANO_TSO (signed), high half reed 2 x SS_I_EPIANO_TSC +
 * SS_WT_OFS (the word reads as a tiny float) */
ZM_INLINE(ss_t_epiano)
static inline uint32_t ss_t_epiano(float s)
{
    float o;
    o = 0.6f * s + 0.3f * (1.0f - 2.0f * s * s);
    return (((uint32_t)(int)(o * SS_I_EPIANO_TSO + 32768.5f) - 32768u) & 0xFFFFu)
         | (uint32_t)(int)(s * SS_I_EPIANO_TSC + (SS_WT_OFS + 0.5f)) << 16;
}

ZM_INLINE(ss_h_epiano)
static inline void ss_h_epiano(SsVoice *v)
{
    v->lp2 = 1.0f; v->bp = 1.0f;
}

/* ss_p_: once per piece of n samples: v->p3 = the tremolo gain at the
 * piece's start x the voice gain g x (1 - DC) (see ss_ic_), its phase ip3
 * (2^32 = one cycle) stepped over the piece */
ZM_INLINE(ss_p_epiano)
static inline void ss_p_epiano(SsVoice *v, uint32_t n, float g)
{
    v->p3 = (0.85f - 0.15f * ss_sin((float)(int)(v->ip3 >> 8) * (1.0f / 16777216.0f)))
            * g * (1.0f - SS_I_EPIANO_DC);
    v->ip3 += (uint32_t)(int)(SS_I_EPIANO_TREM * 4294967296.0f) * n;
}

/* ss_ia_: what is carried from sample to sample (integer phases ip, ip2:
 * the reeds, 2^32 = one cycle; the envelopes as multiply-adds) -> si[0..1]
 * phases, sf[0] level; ss_ib_: the two table reads (-> sf[1]); ss_ic_:
 * soft clip, DC blocker (v->lp), tremolo and gain (v->p3). */
ZM_INLINE(ss_ia_epiano)
static inline void ss_ia_epiano(SsVoice *v, uint32_t *si, float *sf)
{
    float k;
    v->ip += (uint32_t)(int)(v->inc * 4294967296.0f);
    v->ip2 += (uint32_t)(int)(v->inc * (1.003f * 4294967296.0f));   /* second reed */
    k = (float)(int)(v->gate > 0.0f);
    v->lp2 *= SS_I_EPIANO_DEC;                          /* slow decay (1 at a new note) */
    v->env = v->env * (SS_I_EPIANO_REL + k * (1.0f - SS_I_EPIANO_ATT - SS_I_EPIANO_REL))
             + k * SS_I_EPIANO_ATT * v->lp2;
    v->bp *= SS_I_EPIANO_BDEC;                          /* strike (1 at a new note) */
    v->menv = v->menv * (1.0f - SS_I_EPIANO_ATT) + v->bp * SS_I_EPIANO_ATT;
    si[0] = v->ip;
    si[1] = v->ip2;
    sf[0] = v->env * (0.55f + 0.9f * v->menv) * (1.0f / SS_I_EPIANO_TSO);
}

ZM_INLINE(ss_ib_epiano)
static inline float ss_ib_epiano(const uint32_t *si, const float *sf, const uint32_t *t)
{
    float f, f2, oa, ob, ca, cb;
    uint32_t j, j2;
    j = si[0] >> (32u - SS_WT_BITS);                    /* reed 1: low halves, linear */
    f = (float)(int)(si[0] & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
    j2 = si[1] >> (32u - SS_WT_BITS);                   /* reed 2: high halves */
    f2 = (float)(int)(si[1] & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
    oa = (float)((int)(t[j] << 16) >> 16);
    ob = (float)((int)(t[j + 1u] << 16) >> 16);
    ca = (float)(int)(t[j2] >> 16);
    cb = (float)(int)(t[j2 + 1u] >> 16);
    return sf[0] * (oa + (ob - oa) * f + SS_I_EPIANO_RB * (ca + (cb - ca) * f2 - SS_WT_OFS));
}

/* Kept short (cl6x -ms3 overlaps samples only by SPLOOP, at most 48 cycles
 * from a sample's first to last instruction): c = 2 clamp1(x), the soft clip
 * 1.5 x - 0.5 x^3 + 0.15 x^2 written in c; y - lp after the DC step =
 * (1 - DC) (y - lp before it), the (1 - DC) x tremolo x g in v->p3 (ss_p_) */
ZM_INLINE(ss_ic_epiano)
static inline float ss_ic_epiano(SsVoice *v, float x, const float *sf)
{
    float c, y, o;
    (void)sf;
    c = _fabsf(x + 1.0f) - _fabsf(x - 1.0f);
    y = 0.75f * c + c * c * (0.0375f - 0.0625f * c);   /* soft clip + even harmonic */
    o = (y - v->lp) * v->p3;
    v->lp = v->lp * (1.0f - SS_I_EPIANO_DC) + y * SS_I_EPIANO_DC;   /* DC */
    return o;
}
