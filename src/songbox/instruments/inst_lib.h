/* Helper for the instrument engine (instruments/engine.c); mirror of
 * inst_lib.py's psin(). Included by songbox_instr.h. */

/* sin(2 pi p), p in [0, 1): parabolic approximation (psin) */
ZM_INLINE(ss_sin)
static inline float ss_sin(float p)
{
    float x = p * 2.0f - 1.0f, y;
    y = 4.0f * x * (1.0f - _fabsf(x));
    y = 0.225f * (y * _fabsf(y) - y) + y;
    return -y;
}
