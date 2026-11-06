/* Jazz organ (keys): drawbars 888000000, 2 2/3' percussion, key click;
 * also its set's bass (see organ_jazz.py). Constants SS_I_ORGAN_JAZZ_*;
 * mirror: organ_jazz.py. ss_t_: one wavetable word, ss_h_: a new note
 * (v->trig), ss_ia_ / ss_ib_ / ss_ic_: one sample, reading the wavetable t
 * (SS_WT_N + 1 words). */

/* Wavetable word for s = ss_sin(i / SS_WT_N) (16' phase, shared by all
 * tables' row i): low half the drawbar sum s + h2 + h3
 * x SS_I_ORGAN_JAZZ_TSO (signed), high half h2 x SS_I_ORGAN_JAZZ_TSC +
 * SS_WT_OFS (the word reads as a tiny float) */
ZM_INLINE(ss_t_organ_jazz)
static inline uint32_t ss_t_organ_jazz(float s)
{
    float s2, h2, h3, o;
    s2 = s * s;
    h2 = 1.0f - 2.0f * s2;
    h3 = s * (3.0f - 4.0f * s2);
    o = s + h2 + h3;
    return (((uint32_t)(int)(o * SS_I_ORGAN_JAZZ_TSO + 32768.5f) - 32768u) & 0xFFFFu)
         | (uint32_t)(int)(h2 * SS_I_ORGAN_JAZZ_TSC + (SS_WT_OFS + 0.5f)) << 16;
}

ZM_INLINE(ss_h_organ_jazz)
static inline void ss_h_organ_jazz(SsVoice *v)
{
    v->menv = SS_I_ORGAN_JAZZ_RC * 0.8f;                /* percussion, in table units */
    v->bp = SS_I_ORGAN_JAZZ_RC * 0.25f;                 /* click */
}

/* ss_ia_: what is carried from sample to sample (the 16' integer phase ip,
 * 2^32 = one cycle; envelopes) -> si[0] phase, sf[0] level, sf[1]
 * percussion, sf[2] click (both x sf[0]; menv, bp start at their levels x RC); ss_ib_: the drawbar sum
 * at ip, h6 = h2 at 3 ip, h32 = h2 at 16 ip, not interpolated (-> sf[1]);
 * ss_ic_: nothing left to do. */
ZM_INLINE(ss_ia_organ_jazz)
static inline void ss_ia_organ_jazz(SsVoice *v, uint32_t *si, float *sf)
{
    float k;
    v->ip += (uint32_t)(int)(v->inc * 4294967296.0f);  /* 16' phase at inc (the 16' sounds the note) */
    k = (float)(int)(v->gate > 0.0f);
    v->env = v->env * (SS_I_ORGAN_JAZZ_REL + k * (1.0f - SS_I_ORGAN_JAZZ_ATT - SS_I_ORGAN_JAZZ_REL))
             + k * SS_I_ORGAN_JAZZ_ATT;
    v->menv *= SS_I_ORGAN_JAZZ_PERC;
    v->bp *= SS_I_ORGAN_JAZZ_CLICK;
    si[0] = v->ip;
    sf[0] = v->env * (1.0f / SS_I_ORGAN_JAZZ_TSO);
    sf[1] = sf[0] * v->menv;                            /* percussion level */
    sf[2] = sf[0] * v->bp;                              /* click level */
}

/* ss_ib_ kept short (cl6x -ms3 overlaps samples only by SPLOOP, at most 48
 * cycles from a sample's first to last instruction): the levels arrive
 * already x sf[0] x RC, the three parts added as a shallow tree */
ZM_INLINE(ss_ib_organ_jazz)
static inline float ss_ib_organ_jazz(const uint32_t *si, const float *sf, const uint32_t *t)
{
    float f, f6, oa, ob, h6, h32;
    uint32_t ip = si[0], p6 = ip * 3u, j, j6, j32;
    j = ip >> (32u - SS_WT_BITS);                       /* drawbar sum: low halves, linear */
    f = (float)(int)(ip & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
    j6 = p6 >> (32u - SS_WT_BITS);                      /* h6, h32: high halves */
    f6 = (float)(int)(p6 & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
    j32 = (ip << 4) >> (32u - SS_WT_BITS);              /* h32 (a 4 ms click): nearest word */
    oa = (float)((int)(t[j] << 16) >> 16);
    ob = (float)((int)(t[j + 1u] << 16) >> 16);
    h6 = (float)(int)(t[j6] >> 16);
    h6 = (h6 - SS_WT_OFS) + ((float)(int)(t[j6 + 1u] >> 16) - h6) * f6;
    h32 = (float)(int)(t[j32] >> 16) - SS_WT_OFS;
    return (sf[0] * (oa + (ob - oa) * f) + sf[2] * h32) + sf[1] * h6;
}

ZM_INLINE(ss_ic_organ_jazz)
static inline float ss_ic_organ_jazz(SsVoice *v, float x, const float *sf)
{
    (void)v; (void)sf;
    return x;
}
