/* Synth pad (keys): two detuned saws, slow attack, two one-pole
 * low-passes. Constants SS_I_SYNTH_PAD_*; mirror: synth_pad.py.
 * ss_h_: a new note (v->trig; nothing to reset), ss_i_: one sample. */

ZM_INLINE(ss_h_synth_pad)
static inline void ss_h_synth_pad(SsVoice *v)
{
    (void)v;
}

ZM_INLINE(ss_i_synth_pad)
static inline float ss_i_synth_pad(SsVoice *v)
{
    float saw;
    v->p = ss_wrap1(v->p + v->inc * 1.004f);
    v->p2 = ss_wrap1(v->p2 + v->inc * 0.996f);
    v->env = ss_gsel(v, v->env + (1.0f - v->env) * SS_I_SYNTH_PAD_ATT, v->env * SS_I_SYNTH_PAD_REL);
    saw = v->p + v->p2 - 1.0f;
    v->lp += (saw - v->lp) * SS_I_SYNTH_PAD_LP;
    v->lp2 += (v->lp - v->lp2) * SS_I_SYNTH_PAD_LP;
    return v->lp2 * v->env;
}
