/* The instrument record SsInst of the engine (engine.c; Python: engine.py
 * FLOATS + INTS, in this order). Included by songbox_instr.h; the records
 * live in songbox.c's SsState (unpacked from the pool in SsConst). */

typedef struct {
    float pa, pb, pc;           /* phase steps x inc: ip (pa + pb tf), ip2 (pc + pb tf) */
    float att, dec, rel;        /* level envelope; lp2 *= dec (1 at a new note) */
    float bdec, ma, mb;         /* bp *= bdec; menv = menv ma + bp mb */
    float g0, g1;               /* s0 = env (g0 + g1 menv) */
    float r2, q2, b2, b3;       /* second read x s0 (r2 + q2 menv + b2 bp), third x s0 b3 bp */
    float e;                    /* soft clip's even part */
    float la, dc;               /* DC blocker: lp = lp la + y dc (la 0: off) */
    float t0, ts, tt, post;     /* piece gain (t0 + ts sin + tt tri) x post */
    float bp0, mk, m0;          /* new note: bp = bp0, menv = menv mk + m0 */
    uint32_t rot, sh, r0, m2;   /* LFO step; third read at ip << sh; new note: ip2 = ip2 r0 + ip m2 */
} SsInst;
