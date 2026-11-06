/* Helpers shared by the instruments (instruments/<id>.c); mirrors of
 * inst_lib.py's psin(), wrap(), tri() and clip(). Included by songbox_instr.h.
 * The instruments are branch-free (no if / ?: per sample): at
 * --opt_for_space=3 cl6x turns every select into a branch, and a branch in
 * the player's sample loop costs 6+ cycles and stops the scheduler from
 * overlapping the float latencies. */

/* sin(2 pi p), p in [0, 1): parabolic approximation (psin) */
ZM_INLINE(ss_sin)
static inline float ss_sin(float p)
{
    float x = p * 2.0f - 1.0f, y;
    y = 4.0f * x * (1.0f - _fabsf(x));
    y = 0.225f * (y * _fabsf(y) - y) + y;
    return -y;
}

/* p mod 1 for p in [0, 2): a phase after one increment (wrap) */
ZM_INLINE(ss_wrap1)
static inline float ss_wrap1(float p)
{
    return p - (float)(int)p;
}

/* on while the gate is open, else off (exact: k is 0 or 1) */
ZM_INLINE(ss_gsel)
static inline float ss_gsel(const SsVoice *v, float on, float off)
{
    float k = (float)(int)(v->gate > 0.0f);
    return k * on + (1.0f - k) * off;
}

/* triangle LFO in v->p3 (tri; inc = Hz / SR), -1..1 */
ZM_INLINE(ss_tri)
static inline float ss_tri(SsVoice *v, float inc)
{
    v->p3 = ss_wrap1(v->p3 + inc);
    return 4.0f * _fabsf(v->p3 - 0.5f) - 1.0f;
}

/* x clamped to -1..1 (clamp1) */
ZM_INLINE(ss_clamp1)
static inline float ss_clamp1(float x)
{
    return 0.5f * (_fabsf(x + 1.0f) - _fabsf(x - 1.0f));
}

/* cubic soft clip of x clamped to -1..1 (clip): y = 2 clamp1(x),
 * 1.5 c - 0.5 c^3 = y (0.75 - 0.0625 y^2) */
ZM_INLINE(ss_clip)
static inline float ss_clip(float x)
{
    float y = _fabsf(x + 1.0f) - _fabsf(x - 1.0f);
    return y * (0.75f - 0.0625f * y * y);
}
