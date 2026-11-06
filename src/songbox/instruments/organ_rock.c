/* Rock organ (keys): most drawbars from one 16' sine, key click, soft
 * clip, fast rotary (see organ_rock.py). Constants SS_I_ORGAN_ROCK_*; mirror:
 * organ_rock.py. ss_t_: one wavetable word, ss_h_: a new note (v->trig),
 * ss_ia_ / ss_ib_ / ss_ic_: one sample, reading the wavetable t (SS_WT_N + 1
 * words). */

/* Wavetable word for s = ss_sin(i / SS_WT_N) (16' phase i / SS_WT_N, shared
 * by all tables' row i): low half the drawbar sum
 * x SS_I_ORGAN_ROCK_TSO (signed), high half the key click h32
 * x SS_I_ORGAN_ROCK_TSC + SS_WT_OFS (the word reads as a tiny float) */
ZM_INLINE(ss_t_organ_rock)
static inline uint32_t ss_t_organ_rock(float s)
{
    float s2, h2, h3, h4, h6, h8, h16, o, c;
    s2 = s * s;
    h2 = 1.0f - 2.0f * s2;
    h3 = s * (3.0f - 4.0f * s2);
    h4 = 2.0f * h2 * h2 - 1.0f;
    h6 = h2 * (4.0f * h2 * h2 - 3.0f);
    h8 = 2.0f * h4 * h4 - 1.0f;
    h16 = 2.0f * h8 * h8 - 1.0f;
    o = 0.8f * s + h2 + 0.8f * h3 + 0.7f * h4 + 0.5f * h6 + 0.5f * h8 + 0.4f * h16;
    c = 2.0f * h16 * h16 - 1.0f;                        /* click: h32 */
    return (((uint32_t)(int)(o * SS_I_ORGAN_ROCK_TSO + 32768.5f) - 32768u) & 0xFFFFu)
         | (uint32_t)(int)(c * SS_I_ORGAN_ROCK_TSC + (SS_WT_OFS + 0.5f)) << 16;
}

ZM_INLINE(ss_h_organ_rock)
static inline void ss_h_organ_rock(SsVoice *v)
{
    v->menv = 1.0f; v->bp = SS_I_ORGAN_ROCK_CLK0;
}

/* One sample in three steps, run as three loops over a piece (each short
 * enough for cl6x to overlap the samples at -ms3): ss_ia_ steps what is
 * carried from sample to sample (integer phases ip: 16', ip3: rotary LFO,
 * 2^32 = one cycle, wrapping by themselves; the envelope as one
 * multiply-add; the click) and leaves the phase in *si, the rest in
 * sf[0..2]; ss_ib_ reads the wavetable t (-> sf[1]); ss_ic_ the soft clip
 * and the rotary level. */
ZM_INLINE(ss_ia_organ_rock)
static inline void ss_ia_organ_rock(SsVoice *v, uint32_t *si, float *sf)
{
    float tf, k;
    v->ip3 += (uint32_t)(int)(SS_I_ORGAN_ROCK_ROT * 4294967296.0f);
    tf = (float)_abs((int)(v->ip3 >> 1) - 0x40000000);  /* rotary triangle, 0..2^30 */
    v->ip += (uint32_t)(int)(v->inc * (0.997f * 4294967296.0f) + v->inc * 0.024f * tf);  /* inc (16' sounds the note), +-0.3 % */
    k = (float)(int)(v->gate > 0.0f);
    v->env = v->env * (SS_I_ORGAN_ROCK_REL + k * (1.0f - SS_I_ORGAN_ROCK_ATT - SS_I_ORGAN_ROCK_REL))
             + k * SS_I_ORGAN_ROCK_ATT;
    v->bp *= SS_I_ORGAN_ROCK_CLICK;                     /* click level (from SS_I_ORGAN_ROCK_CLK0) */
    si[0] = v->ip;
    sf[0] = v->env * (0.45f / SS_I_ORGAN_ROCK_TSO);
    sf[1] = v->bp;
    sf[2] = 1.0f - (0.4f / 2147483648.0f) * tf;         /* rotary level */
}

ZM_INLINE(ss_ib_organ_rock)
static inline float ss_ib_organ_rock(const uint32_t *si, const float *sf, const uint32_t *t)
{
    float f, oa, ob, ca, cb, x;
    uint32_t j, w0, w1, ip = si[0];
    j = ip >> (32u - SS_WT_BITS);                       /* table word, linear to the next */
    f = (float)(int)(ip & ((1u << (32u - SS_WT_BITS)) - 1u)) * (1.0f / (float)(1u << (32u - SS_WT_BITS)));
    w0 = t[j];
    w1 = t[j + 1u];
    oa = (float)((int)(w0 << 16) >> 16);
    ob = (float)((int)(w1 << 16) >> 16);
    ca = (float)(int)(w0 >> 16);
    cb = (float)(int)(w1 >> 16);
    x = sf[0] * (oa + (ob - oa) * f + sf[1] * (ca + (cb - ca) * f - SS_WT_OFS));
    return x;
}

ZM_INLINE(ss_ic_organ_rock)
static inline float ss_ic_organ_rock(SsVoice *v, float x, const float *sf)
{
    (void)v;
    return ss_clip(x) * sf[2];
}
