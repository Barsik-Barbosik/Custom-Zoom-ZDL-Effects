/* Finger bass: sine plus a low-passed saw, plucked (the low-pass closes).
 * Constants SS_I_FINGER_BASS_*; mirror: finger_bass.py. ss_h_: a new note
 * (v->trig), ss_i_: one sample. */

ZM_INLINE(ss_h_finger_bass)
static inline void ss_h_finger_bass(SsVoice *v)
{
    v->env = 1.0f; v->menv = 1.0f;
}

ZM_INLINE(ss_i_finger_bass)
static inline float ss_i_finger_bass(SsVoice *v)
{
    float saw;
    v->p = ss_wrap1(v->p + v->inc);
    v->env *= ss_gsel(v, SS_I_FINGER_BASS_BDEC, SS_I_FINGER_BASS_BREL);
    v->menv *= SS_I_FINGER_BASS_BLPDEC;
    saw = v->p * 2.0f - 1.0f;
    v->lp += (saw - v->lp) * (SS_I_FINGER_BASS_BLP * (0.25f + 0.75f * v->menv));
    return (0.8f * ss_sin(v->p) + 0.6f * v->lp) * v->env;
}
