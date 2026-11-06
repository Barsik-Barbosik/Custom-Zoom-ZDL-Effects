/* Synth bass: saw through a resonant state-variable low-pass whose
 * frequency follows a decaying envelope. Constants SS_I_SYNTH_BASS_*;
 * mirror: synth_bass.py. ss_h_: a new note (v->trig), ss_i_: one sample. */

ZM_INLINE(ss_h_synth_bass)
static inline void ss_h_synth_bass(SsVoice *v)
{
    v->env = 1.0f; v->menv = 1.0f;
}

ZM_INLINE(ss_i_synth_bass)
static inline float ss_i_synth_bass(SsVoice *v)
{
    float f, saw, hp;
    v->p = ss_wrap1(v->p + v->inc);
    v->env *= ss_gsel(v, SS_I_SYNTH_BASS_BDEC, SS_I_SYNTH_BASS_BREL);
    v->menv *= SS_I_SYNTH_BASS_FDEC;
    f = 0.02f + 0.18f * v->menv;        /* SVF frequency coefficient */
    saw = v->p * 2.0f - 1.0f;
    hp = saw - v->lp - 0.5f * v->bp;    /* q = 0.5: some resonance */
    v->bp += f * hp;
    v->lp += f * v->bp;
    return v->lp * v->env;
}
