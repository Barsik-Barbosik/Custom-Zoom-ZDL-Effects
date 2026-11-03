/* Synth cymbal code:
 * the metal bank at x mult ring-modulated (sign products of square pairs:
 * dense, no pitch) mixed with low-passed noise, two one-pole high-passes,
 * a low-pass that closes as the hit decays (dark, lp_max), an optional
 * swell (rise rate swell_rate) and a short
 * metal stick ping. Plays the ride (mult 12), hats and
 * crash (synth_ride.py, synth_hat.py etc.); dark 0 + lp_max 1 = no closing low-pass, noise_lp 1 = plain
 * noise. Params P_SYNTH_CYMBAL_*; mirror: synth_cymbal.py. */

typedef struct SynthCymbal {
    float amp, lp1, lp2, nlp, lp, att, ping, p1, hm[6], m[6];   /* m: the squares at x mult */
} SynthCymbal;
DB_SYN_FITS(SynthCymbal);

DB_ALWAYS_INLINE(db_syn_synth_cymbal_hit)
static inline void db_syn_synth_cymbal_hit(DBSyn *v, const float *p, float vel)
{
    SynthCymbal *y = (SynthCymbal *)v->s;
    y->amp = vel;
    y->ping = vel;
    y->att = 1.0f - p[P_SYNTH_CYMBAL_SWELL];
}

DB_ALWAYS_INLINE(db_syn_synth_cymbal_render)
static inline float db_syn_synth_cymbal_render(DBSyn *v, const float *p)
{
    SynthCymbal *y = (SynthCymbal *)v->s;
    float *hm = y->hm, *m = y->m;
    float n, c, rmC, src, hp, k, o;
    int j;
    if (y->amp <= 0.00005f) {
        y->amp = 0.0f;
        y->ping = 0.0f;
        v->active = 0;
        return 0.0f;
    }
    n = db_noise(v);
#pragma UNROLL(1)
    for (j = 0; j < 6; j++) {
        float h = db_wrap(hm[j] + db_metal_inc[j]);
        hm[j] = h;
        m[j] = db_sq(db_frac(h * p[P_SYNTH_CYMBAL_MULT]));
    }
    c = p[P_SYNTH_CYMBAL_HP];
    rmC = (m[0] * m[3] + m[1] * m[4] + m[2] * m[5] + m[0] * m[5] * m[2]) * 0.25f;
    y->nlp += (n - y->nlp) * p[P_SYNTH_CYMBAL_NOISE_LP];
    src = db_lerp(y->nlp * 2.0f, rmC, p[P_SYNTH_CYMBAL_METAL]);
    y->lp1 += (src - y->lp1) * c;
    hp = src - y->lp1;
    y->lp2 += (hp - y->lp2) * c;
    hp = hp - y->lp2;
    k = 1.0f - p[P_SYNTH_CYMBAL_DARK] * (1.0f - y->amp);
    k = 0.08f + (p[P_SYNTH_CYMBAL_LP_MAX] - 0.08f) * k * k;
    y->lp += (hp - y->lp) * k;
    if (y->att < 1.0f)
        y->att += (1.0f - y->att) * p[P_SYNTH_CYMBAL_SWELL_RATE];
    o = y->lp * y->amp * y->att;
    if (y->ping > 0.00005f) {
        y->p1 += (rmC - y->p1) * 0.85f;
        o += (rmC - y->p1) * y->ping * p[P_SYNTH_CYMBAL_PING];
        y->ping *= 0.9985f;
    }
    y->amp *= p[P_SYNTH_CYMBAL_DEC];
    return o * p[P_SYNTH_CYMBAL_LVL];
}
