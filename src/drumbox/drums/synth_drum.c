/* Synth drum code: sine body with a
 * small, fast pitch drop, a "skin" partial at punch_ratio x pitch, mild
 * drive and a low-passed decaying beater click. Plays the kick
 * (synth_kick.py), the toms (punch, drive and click off) and the snare (no
 * sweep, punch = its second sine, the click = its noise, high-passed by
 * noise_hp, phases start at phase0); it plays until both the body and the
 * click have decayed. Params
 * P_SYNTH_DRUM_*; Python mirror: synth_drum.py. Crush (LoFi kit):
 * crush_rate > 0 holds the output, resampled crush_rate x 44.1 kHz,
 * truncated to steps of crush_step (crush_q = 1 / crush_step); 0 = off. */

typedef struct SynthDrum {
    float phase, amp, pitch, click, clickLp, punchPh, punch, hpLp, hold, held;
} SynthDrum;
DB_SYN_FITS(SynthDrum);

DB_ALWAYS_INLINE(db_syn_synth_drum_hit)
static inline void db_syn_synth_drum_hit(DBSyn *v, const float *p, float vel)
{
    SynthDrum *k = (SynthDrum *)v->s;
    k->amp = vel;
    k->pitch = 1.0f;
    k->click = vel;
    k->punch = vel;
    k->phase = p[P_SYNTH_DRUM_PHASE0];  /* kick: 0, a zero crossing: no step click */
    k->punchPh = p[P_SYNTH_DRUM_PHASE0];
    k->hpLp = 0.0f;             /* the lane's last drum may have left it set */
    k->hold = 1.0f;             /* crush: take the first sample */
    k->held = 0.0f;
}

DB_ALWAYS_INLINE(db_syn_synth_drum_render)
static inline float db_syn_synth_drum_render(DBSyn *v, const float *p)
{
    SynthDrum *k = (SynthDrum *)v->s;
    float n, f, ks, out;
    if (k->amp <= 0.00005f && k->click <= 0.00005f) {
        k->amp = 0.0f;
        v->active = 0;
        return 0.0f;
    }
    n = db_noise(v);
    f = p[P_SYNTH_DRUM_F] * (1.0f + p[P_SYNTH_DRUM_SWEEP] * k->pitch);
    k->phase = db_wrap(k->phase + f * DB_INV_SR);
    ks = db_sin(k->phase) * k->amp;
    if (p[P_SYNTH_DRUM_PUNCH] > 0.0f) {
        k->punchPh = db_wrap(k->punchPh + f * p[P_SYNTH_DRUM_PUNCH_RATIO] * DB_INV_SR);
        ks += db_sin(k->punchPh) * k->punch * p[P_SYNTH_DRUM_PUNCH];
        k->punch *= p[P_SYNTH_DRUM_PUNCH_DEC];
    }
    if (p[P_SYNTH_DRUM_DRIVE] > 0.0f)
        ks = db_soft_clip(ks * (1.0f + p[P_SYNTH_DRUM_DRIVE]));
    out = ks * p[P_SYNTH_DRUM_LVL];
    k->hpLp += (n - k->hpLp) * p[P_SYNTH_DRUM_NOISE_HP];    /* 0: hpLp stays 0 */
    n -= k->hpLp;
    k->clickLp += (n - k->clickLp) * p[P_SYNTH_DRUM_CLICK_LP];
    out += k->clickLp * k->click * p[P_SYNTH_DRUM_CLICK];
    k->click *= p[P_SYNTH_DRUM_CLICK_DEC];
    k->pitch *= p[P_SYNTH_DRUM_SWEEP_DEC];
    k->amp *= p[P_SYNTH_DRUM_DEC];
    if (p[P_SYNTH_DRUM_CRUSH_RATE] > 0.0f) {
        if (k->hold >= 1.0f) {
            k->hold -= 1.0f;
            k->held = (float)(int)(out * p[P_SYNTH_DRUM_CRUSH_Q]) * p[P_SYNTH_DRUM_CRUSH_STEP];
        }
        k->hold += p[P_SYNTH_DRUM_CRUSH_RATE];
        out = k->held;
    }
    return out;
}
