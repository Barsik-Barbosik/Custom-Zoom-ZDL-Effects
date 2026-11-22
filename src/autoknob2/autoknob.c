/*
 * AutoKnob 2 -- two LFOs, each moving a knob of another effect in the
 * chain, stock effects included. No sound of its own (audio passes through).
 *
 * How: the target's knob table (per-slot info word 0) gives the knob's
 * edit handler. AutoKnob writes the new value into the target's state[31]
 * table column, then calls that handler with a copy of the target's ctx
 * (per-slot, 53 words) in which the OS services that wait for the audio
 * thread are replaced (they would hang inside an audio call):
 *   ctx[7], ctx[8]  set a float param with a ramp -> ak_ramp (own ramp:
 *                   linear over AK_GAP calls, run by AutoKnob's audio call)
 *   ctx[9], ctx[10] set a param                    -> ak_store (at once)
 *   ctx[15]         copy a coefficient block       -> ctx[34] (the OS's
 *                   plain copy, as when the audio thread isn't running)
 *   ctx[32]         reroute the chain              -> ak_nop (skipped)
 * The handler computes its params as on a knob turn.
 * The patch record (what a save keeps and the knob page shows) is never
 * written: the target knob goes back to that value when the LFO stops.
 *
 * Firmware: ak_fwt holds the known OSes (G1on family 1.21: HW-tested;
 * MS-70CDR 2.10, MS-50G 3.10, MS-60B 2.10: addresses from the firmware
 * images, untested). Before any handler call the target's ctx words 7, 8,
 * 9, 10, 15 and 32 must hold exactly that OS's functions; on an unknown
 * OS, or a mismatch, AutoKnob does nothing.
 *
 * Knob menu: knob 1 / 4 picks the row of LFO 1 / 2, knobs 2-3 / 5-6 are
 * that row's two values (names and max rewritten in the descriptor per
 * row), knobs 7-9 are locked storage. Rows: Target (Slot: Off or a chain
 * position, text = the effect's name / Knob: text = the knob's name, max =
 * the target's last knob),
 * Wave (Shape: Sine Tri Saw Ramp Sqr S&H / Rate 0..100 = 0.05..10 Hz), Range
 * (Low / High, 0..100 % of the knob's range; Low > High sweeps upside
 * down), Other (Phase 0..345 deg in 15 deg steps / Show Off, On).
 *
 * Storage: the values of all rows live in the state (v). The visible row's
 * two are the knobs the user turns; the other three rows are packed into a
 * 30-bit hidden stream per LFO: LFO 1 = knob 1 bits 0-10 + knob 8 bits
 * 0-18, LFO 2 = knob 4 bits 0-5 + knob 7 + knob 9 + knob 8 bits 19-26
 * (rows in order from bit 0, the visible one left out; Target = Slot * 9 +
 * Knob in 6 bits, Wave = Shape << 7 | Rate, Range = Low << 7 | High,
 * Other = Phase << 1 | Show). A selector holds row << 11 / 6 | its stream
 * bits, so the knob page points into the row's quarter. ak_sync (edit
 * handler of knobs 1, 4, 7, 8, 9, and AK_MENU) folds the visible values
 * in, moves a turned selector one row (direction of the firmware's new
 * value), encodes and writes every own column that differs (table and
 * record rows); handler and tick run the same idempotent step. The values
 * are taken from the patch AK_LWAIT syncs after a patch change, a chain
 * rebuild or a slot change (decode, clamp); the LFOs wait for them.
 *
 * LFOs: same Rate = LFO 2 runs on LFO 1's phase (locked; Phase sets the
 * offset), else on its own. Both on the same knob: LFO 1 wins, LFO 2
 * idles (its Knob text "---"). Footswitch off or a target change: the old
 * target knob goes back to its own setting. Never a target: Floppy (its
 * knobs hold saved data) and AutoKnob itself.
 *
 * Show On (OSes with a known saved-patch copy only; MS rows: static, untested):
 * the LFO value also goes into the target's patch record, and the knob page
 * is redrawn (Event_post, ~30 / s while the target is the selected effect),
 * so the page follows the LFO. The same value goes into the saved-patch
 * copy, so the patch doesn't count as edited (no EDITED, no Auto Save). The
 * knob's own setting (uval) and the saved copy's value (sval) are kept and
 * written back when Show goes off, the footswitch goes off or the target
 * changes; a record value the LFO didn't write is a knob turn: the new
 * setting. A patch saved while Show is on keeps the LFO value of that
 * moment.
 *
 * CPU: every call only checks the patch number and the chain-rebuild flags
 * and runs the ramps. A cycle of AK_GAP calls: call 0 (the tick) computes
 * LFO 1 (target checks, LFO), AK_MENU syncs the menu (every other cycle;
 * knob names / max only after a row change), AK_FIRE0 calls LFO 1's
 * handler, AK_LFO2 computes LFO 2, AK_FIRE1 calls its handler (each part
 * reads tables the other effects have pushed out of the cache, so apart
 * they peak lower). The firmware row is found once and kept in the state
 * (unknown firmware: looked for again every AK_FW_WAIT calls).
 */

#include <stdint.h>

#include "autoknob_rates.h"

#pragma CODE_SECTION(Fx_SFX_AutoKnob, ".audio")

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))
#define AK_U32(a) (*(const volatile uint32_t *)(uintptr_t)(a))

#define AK_FLOPPY    ((7u << 24) | 522u)  /* Floppy: knobs = stored data, never moved */
#define AK_AK1       ((7u << 24) | 523u)  /* AutoKnob 1.x */
#define AK_AK2       ((7u << 24) | 524u)  /* AutoKnob 2 release build */
#ifdef AK_PROBE
#define AK_ID        ((7u << 24) | 1011u) /* own record word +4 (probe id SFX 0x3F3) */
#else
#define AK_ID        AK_AK2               /* own record word +4 */
#endif
/* never a target: no effect, any AutoKnob (own build, 1.x, 2), Floppy */
#define AK_NOGO(id)  ((id) == 0u || (id) == AK_ID || (id) == AK_AK1 || (id) == AK_AK2 || \
                      (id) == AK_FLOPPY)
#define AK_DESC      _Fx_SFX_AutoKnob_Desc
#define AK_CTX0      0x11F03000u      /* per-slot ctx, 212 B (53 words) each (all OSes) */
#define AK_CTX_STEP  212u
#define AK_CTX_WORDS 53u
#define AK_INFO_STEP 28u              /* per-slot info; word 0 = knob table */
#define AK_ENTRY     48u              /* knob table entry */
#define AK_E_MAX     0x0Cu            /* entry: max value */
#define AK_E_EDIT    0x1Cu            /* entry: edit handler */
#define AK_E_FLAGS   0x2Cu            /* entry: flags, 0x04 = last entry */
#define AK_MAGIC     0x414B3232u      /* "AK22"; new value on any state layout change */
#define AK_POST_GAP  184u             /* Show: calls between redraws (~30 / s) */
#define AK_FW_WAIT   4096u            /* unknown firmware: calls between looks */
#define AK_NONE      0xFFFFFFFFu
#define AK_GAP       32u              /* calls per cycle = between handler calls (~172 Hz) */
#define AK_MENU      4u               /* cycle call: menu sync (every other cycle) */
#define AK_FIRE0     8u               /* cycle call: LFO 1's handler call */
#define AK_LFO2      16u              /* cycle call: LFO 2 computed */
#define AK_FIRE1     24u              /* cycle call: LFO 2's handler call */
#define AK_INV_GAP   (1.0f / 32.0f)
#define AK_NRAMP     16u              /* params ramping at once */
#define AK_AREA      0x400u           /* only params within this many bytes of the
                                       * target's ctx[1] / ctx[2] ramp */
#define AK_STABLE    512u             /* calls the target must stay unchanged (~93 ms) */

/* menu */
#define AK_LWAIT     16u              /* menu syncs after a change before the values are taken */
#define AK_NROWS     4u               /* rows per LFO */
#define AK_NVAL      8u               /* values per LFO (2 per row) */
#define AK_SCOL(i)   ((i) ? 5u : 2u)  /* selector column: knob 1 / 4 */
#define AK_ACOL(i)   ((i) ? 6u : 3u)  /* value A column: knob 2 / 5 (B = A + 1) */
#define AK_SSH(i)    ((i) ? 6u : 11u) /* hidden stream bits below the row */

/* values (index = 2 * row + 0 / 1) */
#define AK_V_SLOT    0u               /* 0 = Off, 1.. = chain position + 1 */
#define AK_V_KNOB    1u               /* 0..8 = knob 1..9 */
#define AK_V_WAVE    2u
#define AK_V_RATE    3u
#define AK_V_LOW     4u
#define AK_V_HIGH    5u
#define AK_V_PHASE   6u               /* 15 deg steps */
#define AK_V_SHOW    7u

#define AK_WAVE_SINE 0
#define AK_WAVE_TRI  1
#define AK_WAVE_SAW  2
#define AK_WAVE_RAMP 3
#define AK_WAVE_SQR  4
#define AK_WAVE_SH   5
#define AK_NWAVES    6u

/* One known OS. Fingerprint as zmenu.h's zm_fw_ok: the words at post..+16
 * are ak_fwt.w[0..2], val[0], val[1]; then the word at sig[i] is val[2 + i]
 * (sig 0 ends). rec / tab: patch record and state[31] table rows (44 B per
 * slot, word +4 = gid << 24 | fxid, cols 2..10 = knobs); patch: current
 * patch number; info: per-slot info (AK_INFO_STEP B, word 0 = knob table);
 * fdir / fcopy: the OS's "write directly" flags (the B14 words tested by
 * the setters / the block copy; set while the OS rebuilds the chain); fn:
 * the OS's ctx[7], [8], [9], [10], [15], [32]. For Show: sav = the saved
 * patch copy's rows (as rec; 0 = unknown: no Show), event = where the
 * screen task's event object pointer is kept (post = Event_post), cursor =
 * the selected effect's byte (= its slot). */
typedef struct {
    uint32_t post, val[7], sig[5];
    uint32_t rec, tab, patch, info;
    uint32_t fdir, fcopy;
    uint32_t fn[6];
    uint32_t slots;
    uint32_t sav, event, cursor;
} AkFw;

#define AK_NFW 4u
static const struct { uint32_t w[3]; AkFw row[AK_NFW]; } ak_fwt = {
  { 0x27773577u, 0x85778677u, 0xB64631F7u }, {
    /* G1on family (G1on / G1Xon 1.21, B1on / B1Xon 1.30): HW (B1Xon 1.30).
     * B14 0xC00DEA00: fdir B14[94], fcopy B14[93]. */
    { 0xC00B05C0u, { 0x02C53A2Bu, 0x01C50628u, 0x0200736Du, 0x10089013u,
        0x02C65A28u, 0x02104CA0u, 0u },
      { 0xC009B278u, 0xC009B280u, 0xC008B318u, 0xC008B320u, 0u },
      0x1181B160u, 0x1181B370u, 0xC00DEADCu, 0xC003F204u,
      0xC00DEB78u, 0xC00DEB74u,
      { 0xC00B9344u, 0xC00B92C0u, 0xC00BE860u, 0xC00BE7E0u, 0xC00C11B0u, 0xC00A5E84u },
      5u, 0x1181B268u, 0xC00DEBCCu, 0xC0078CB4u },
    /* MS-70CDR 2.10 (fingerprint, rows, patch as zmenu.h). B14 0xC00EF748:
     * fdir B14[98], fcopy B14[149]. UNTESTED. */
    { 0xC00CBF40u, { 0x02E7442Bu, 0x01E74228u, 0x1021BC13u, 0x0200A46Du,
        0x077BA42Au, 0x0760076Au, 0x033CFFA9u },
      { 0xC00BB168u, 0xC00BB16Cu, 0xC00DD818u, 0xC00DD81Cu, 0xC00BADA8u },
      0xC009BF58u, 0xC009C1A0u, 0xC00EF88Cu, 0xC0064E04u,
      0xC00EF8D0u, 0xC00EF99Cu,
      { 0xC00CC94Cu, 0xC00CC8C8u, 0xC00D8C80u, 0xC00D8C00u, 0xC00DD4B0u, 0xC00C3B04u },
      6u, 0xC009C07Cu, 0xC00EF9D8u, 0xC009C064u },  /* sav: IS_DIRTY 0xC00B8534, 292 B */
    /* MS-50G 3.10. B14 0xC00EFCC0: fdir B14[101], fcopy B14[151]. UNTESTED. */
    { 0xC00CC680u, { 0x02E9C02Bu, 0x01E9BE28u, 0x1041E013u, 0x0200A96Cu,
        0x077E602Au, 0x0760076Au, 0x033CFFA9u },
      { 0xC00AB784u, 0xC00AB788u, 0xC00DDCD8u, 0xC00DDCDCu, 0xC00BA650u },
      0xC009BF58u, 0xC009C1A0u, 0xC00EFE0Cu, 0xC0064E04u,
      0xC00EFE54u, 0xC00EFF1Cu,
      { 0xC00CD08Cu, 0xC00CD008u, 0xC00D9100u, 0xC00D9080u, 0xC00DD970u, 0xC00C3C84u },
      6u, 0xC009C07Cu, 0xC00EFF64u, 0xC009C064u },  /* sav: IS_DIRTY 0xC00B87F4, 292 B */
    /* MS-60B 2.10. B14 0xC00DFCF0: fdir B14[100], fcopy B14[150]. UNTESTED. */
    { 0xC00BC500u, { 0x02EA982Bu, 0x01EA9628u, 0x1041E413u, 0x0200A66Cu,
        0x077E782Au, 0x076006EAu, 0x033CFFA9u },
      { 0xC009B5E4u, 0xC009B5E8u, 0xC00CDEB8u, 0xC00CDEBCu, 0xC00AA664u },
      0xC008C6A0u, 0xC008C838u, 0xC00DFE3Cu, 0xC0047204u,
      0xC00DFE80u, 0xC00DFF48u,
      { 0xC00BCF0Cu, 0xC00BCE88u, 0xC00C9300u, 0xC00C9280u, 0xC00CDB50u, 0xC00B31C4u },
      4u, 0xC008C76Cu, 0xC00DFF88u, 0xC008C754u },  /* sav: IS_DIRTY 0xC00A7E0C, 204 B */
} };

/* ctx words that must hold the OS's fn[] (replaced in the borrowed copy) */
#define AK_C_SET     7u               /* set a float param with a ramp */
#define AK_C_POST    8u               /* post (dst, value, step), wait */
#define AK_C_SET9    9u
#define AK_C_SET10   10u
#define AK_C_COPY    15u              /* copy a coefficient block, wait */
#define AK_C_ROUTE   32u              /* reroute the chain, wait */
#define AK_C_MEMCPY  34u              /* the OS's plain copy */

/* One LFO's engine. */
typedef struct {
    uint32_t ts, col, id;             /* current target (AK_NONE = none) */
    uint32_t tab, hand;               /* its knob table and edit handler */
    uint32_t stable;                  /* calls the target has stayed unchanged */
    uint32_t active;                  /* the target's table holds an LFO value */
    uint32_t last;                    /* value last written */
    uint32_t rnd;                     /* S&H generator */
    float phase, lastq, hold;         /* phase 0..1, last phase with offset, S&H value */
    uint32_t uval;                    /* the target knob's own setting */
    uint32_t sval;                    /* Show: the saved copy's value before */
    uint32_t shown;                   /* Show: record / saved copy hold rlast */
    uint32_t rlast;                   /* Show: value last written there */
    uint32_t pgap, pdirty;            /* Show: calls since the last redraw, redraw due */
    uint32_t pend, pw, pshow;         /* handler call due: value, Show */
} AkLfo;

/* State at the ctx[3] arena's end, magic at both ends. */
typedef struct {
    uint32_t magic0;
    uint32_t copy[AK_CTX_WORDS];      /* the borrowed ctx */
    AkLfo l[2];
    uint32_t patch;                   /* patch number the fields belong to */
    uint32_t fwi;                     /* ak_fwt row + 1 (0 = not found yet) */
    uint32_t fwwait;                  /* calls until the next look (unknown firmware) */
    uint32_t tick;                    /* calls since the last tick */
    uint32_t rtop;                    /* ramps in use are below this index */
    uint32_t rdst[AK_NRAMP];          /* ramping params (0 = free) */
    uint32_t rleft[AK_NRAMP];         /* calls left */
    float rtgt[AK_NRAMP], rstep[AK_NRAMP];
    uint32_t lok, lslot, lwait;       /* menu values valid, for slot, ticks waited */
    uint32_t row[2];                  /* selected row per LFO */
    uint32_t v[2][AK_NVAL];           /* Slot Knob Wave Rate Low High Phase Show */
    uint32_t sel[2];                  /* selector columns as last written */
    uint32_t drow[2];                 /* rows the descriptor's names / max show */
    uint32_t cyc;                     /* cycle parity (menu sync every other cycle) */
    uint32_t s[2];                    /* scratch: hidden streams */
    uint32_t e[11];                   /* scratch: columns 2..10 as they should be */
    uint32_t magic1;
} AkState;

/* The state (set by every audio call), for ak_ramp, Ak_MenuEdit and the
 * knob texts (the 4-byte .fardata; each slot runs its own code image). */
static volatile uint32_t ak_g = 0u;

extern unsigned int AK_DESC[];

typedef void (*AkHandler)(uint32_t *ctx);

/* ctx[9] / ctx[10]'s replacement: A4 = destination, B4 = value (float bits
 * or int; stored as they are). */
#pragma FUNC_CANNOT_INLINE(ak_store)
#pragma CODE_SECTION(ak_store, ".text")
void ak_store(uint32_t *dst, uint32_t v)
{
    *(volatile uint32_t *)dst = v;
}

/* ctx[32]'s replacement: no chain reroute. */
#pragma FUNC_CANNOT_INLINE(ak_nop)
#pragma CODE_SECTION(ak_nop, ".text")
void ak_nop(void)
{
}

/* ctx[7] / ctx[8]'s replacement: A4 = float param, B4 = new value. Params
 * near the target's ctx[1] / ctx[2] ramp there linearly over AK_GAP calls
 * (AutoKnob's audio call moves them); anything else, or no free ramp, is
 * stored at once. */
#pragma FUNC_CANNOT_INLINE(ak_ramp)
#pragma CODE_SECTION(ak_ramp, ".text")
void ak_ramp(float *dst, float v)
{
    volatile AkState *st = (volatile AkState *)(uintptr_t)ak_g;
    uint32_t a = (uint32_t)(uintptr_t)dst, i, f = AK_NRAMP;
    float cur = *(volatile float *)dst;
    if (st && (a & 3u) == 0u && cur - cur == 0.0f &&
        (a - st->copy[1] < AK_AREA || a - st->copy[2] < AK_AREA)) {
#pragma UNROLL(1)
        for (i = 0u; i < AK_NRAMP; i++) {
            if (st->rdst[i] == a) { f = i; break; }
            if (st->rdst[i] == 0u && f == AK_NRAMP) f = i;
        }
        if (f < AK_NRAMP) {
            st->rdst[f] = a;
            st->rtgt[f] = v;
            st->rstep[f] = (v - cur) * AK_INV_GAP;
            st->rleft[f] = AK_GAP;
            if (st->rtop <= f) st->rtop = f + 1u;
            return;
        }
    }
    *(volatile float *)dst = v;
}

/* Write value v into slot ts / column col of the state[31] table rows tab
 * and run the knob's edit handler hand with a fresh borrowed copy of the
 * slot's ctx. */
#pragma FUNC_CANNOT_INLINE(ak_poke)
#pragma CODE_SECTION(ak_poke, ".text")
void ak_poke(volatile AkState *st, uint32_t tab, uint32_t ts, uint32_t col, uint32_t hand,
             uint32_t v)
{
    volatile const uint32_t *src = (volatile const uint32_t *)(uintptr_t)(AK_CTX0 + AK_CTX_STEP * ts);
    uint32_t i, ramp = (uint32_t)(uintptr_t)ak_ramp, store = (uint32_t)(uintptr_t)ak_store;
#pragma UNROLL(1)
    for (i = 0u; i < AK_CTX_WORDS; i++) st->copy[i] = src[i];
    st->copy[AK_C_SET] = ramp;
    st->copy[AK_C_POST] = ramp;
    st->copy[AK_C_SET9] = store;
    st->copy[AK_C_SET10] = store;
    st->copy[AK_C_COPY] = st->copy[AK_C_MEMCPY];
    st->copy[AK_C_ROUTE] = (uint32_t)(uintptr_t)ak_nop;
    *(volatile uint32_t *)(uintptr_t)(tab + 44u * ts + 4u * col) = v;
    ak_g = (uint32_t)(uintptr_t)st;
    ((AkHandler)(uintptr_t)hand)((uint32_t *)st->copy);
}

/* ---- knob menu ---- */

static const uint32_t ak_vmax[AK_NVAL] = { 6u, 8u, AK_NWAVES - 1u, AK_NRATES - 1u, 100u, 100u,
                                           23u, 1u };
static const uint32_t ak_bsh[AK_NROWS] = { 0u, 7u, 7u, 1u };    /* row = A << bsh | B (Target: A * 9 + B) */
static const uint32_t ak_rw[AK_NROWS] = { 6u, 10u, 14u, 6u };   /* packed row widths */
static const char ak_vname[AK_NVAL][8] = { "Slot", "Knob", "Shape", "Rate", "Low", "High",
                                           "Phase", "Show" };
static const char ak_rname[AK_NROWS][8] = { "TARGET", "WAVE", "RANGE", "OTHER" };

/* Max of value k (Slot: Off + the OS's chain positions). */
#pragma FUNC_ALWAYS_INLINE(ak_max)
static inline uint32_t ak_max(const AkFw *f, uint32_t k)
{
    return k == AK_V_SLOT ? f->slots : ak_vmax[k];
}

#pragma FUNC_CANNOT_INLINE(ak_kmax)
#pragma CODE_SECTION(ak_kmax, ".text")
uint32_t ak_kmax(const AkFw *f, uint32_t sv, uint32_t own);   /* below */

/* Sync the menu columns of the own slot with the state; load: take the
 * values from the record row first (decode). Returns 1 if a column or the
 * descriptor was written. */
#pragma FUNC_CANNOT_INLINE(ak_sync)
#pragma CODE_SECTION(ak_sync, ".text")
uint32_t ak_sync(volatile AkState *st, const AkFw *f, uint32_t own, uint32_t load)
{
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(f->rec + 44u * own);
    volatile uint32_t *t = (volatile uint32_t *)(uintptr_t)(f->tab + 44u * own);
    volatile unsigned int *d = (volatile unsigned int *)AK_DESC;
    volatile const uint32_t *src = load ? r : t;
    uint32_t i, j, b, c, k, n, x, a, m, ch = 0u;

    /* nothing turned (every column as last written): done (the common case;
     * keeps the cold work below to the calls after a turn) */
    if (!load) {
#pragma UNROLL(1)
        for (c = 2u; c <= 10u; c++)
            if (t[c] != st->e[c] || r[c] != st->e[c]) break;
        if (c > 10u) return 0u;
    }

    /* load: rows and the hidden rows' values from the stored columns */
    if (load) {
        st->drow[0] = AK_NONE;             /* names / max written again */
        st->drow[1] = AK_NONE;
        st->s[0] = (r[2] & 0x7FFu) | (r[9] & 0x7FFFFu) << 11;
        st->s[1] = (r[5] & 0x3Fu) | (r[8] & 0xFFu) << 6 | (r[10] & 0xFFu) << 14 |
                   (r[9] >> 19 & 0xFFu) << 22;
#pragma UNROLL(1)
        for (i = 0u; i < 2u; i++) {
            st->row[i] = r[AK_SCOL(i)] >> AK_SSH(i) & (AK_NROWS - 1u);
            n = 0u;
#pragma UNROLL(1)
            for (j = 0u; j < AK_NROWS; j++) {
                if (j == st->row[i]) continue;
                x = st->s[i] >> n & ((1u << ak_rw[j]) - 1u);
                n += ak_rw[j];
                if (j == 0u) {
                    a = (x * 57u) >> 9;            /* x / 9, exact for x < 64 */
                    b = x - 9u * a;
                } else {
                    a = x >> ak_bsh[j];
                    b = x & ((1u << ak_bsh[j]) - 1u);
                }
                st->v[i][2u * j] = a;
                st->v[i][2u * j + 1u] = b;
            }
        }
    }

    /* the visible row's two values (the knobs the user turns) */
#pragma UNROLL(1)
    for (i = 0u; i < 2u; i++) {
        st->v[i][2u * st->row[i]] = src[AK_ACOL(i)];
        st->v[i][2u * st->row[i] + 1u] = src[AK_ACOL(i) + 1u];
    }

    /* all values clamped (garbage from another layout, a max not yet in force;
     * after a turn also Knob to the target's last knob: not at load, while
     * the targets may still be loading) */
#pragma UNROLL(1)
    for (k = 0u; k < 2u * AK_NVAL; k++) {
        i = k >= AK_NVAL;
        j = k - AK_NVAL * i;
        m = j == AK_V_KNOB && !load ? ak_kmax(f, st->v[i][AK_V_SLOT], own) : ak_max(f, j);
        if (st->v[i][j] > m) st->v[i][j] = m;
    }

    /* a turned selector: one row towards the firmware's new value */
    if (!load) {
#pragma UNROLL(1)
        for (i = 0u; i < 2u; i++) {
            x = t[AK_SCOL(i)];
            j = st->row[i];
            if (x > st->sel[i] && j < AK_NROWS - 1u) j++;
            else if (x < st->sel[i] && j > 0u) j--;
            st->row[i] = j;
        }
    }

    /* encode: the hidden streams (rows other than the visible one, in row
     * order from bit 0), then the columns */
#pragma UNROLL(1)
    for (i = 0u; i < 2u; i++) {
        uint32_t s = 0u;
        n = 0u;
#pragma UNROLL(1)
        for (j = 0u; j < AK_NROWS; j++) {
            if (j == st->row[i]) continue;
            a = st->v[i][2u * j];
            b = st->v[i][2u * j + 1u];
            x = j == 0u ? 9u * a + b : a << ak_bsh[j] | b;
            s |= x << n;
            n += ak_rw[j];
        }
        st->s[i] = s;
    }
    st->e[2] = st->row[0] << AK_SSH(0) | (st->s[0] & 0x7FFu);
    st->e[5] = st->row[1] << AK_SSH(1) | (st->s[1] & 0x3Fu);
    st->e[8] = st->s[1] >> 6 & 0xFFu;
    st->e[9] = (st->s[0] >> 11 & 0x7FFFFu) | (st->s[1] >> 22 & 0xFFu) << 19;
    st->e[10] = st->s[1] >> 14 & 0xFFu;
    st->sel[0] = st->e[2];
    st->sel[1] = st->e[5];

    /* the value knobs: the max of their value (Knob: the target's knob
     * count) and after a row change the name (descriptor entry = column;
     * max first, so a value is never above it), then the value */
#pragma UNROLL(1)
    for (k = 0u; k < 4u; k++) {
        i = k >> 1;
        b = k & 1u;
        c = AK_ACOL(i) + b;
        j = 2u * st->row[i] + b;
        m = j == AK_V_KNOB ? ak_kmax(f, st->v[i][AK_V_SLOT], own) : ak_max(f, j);
        if (d[12u * c + 3u] != m) {
            d[12u * c + 3u] = m;
            ch = 1u;
        }
        if (st->drow[i] != st->row[i]) {
            volatile unsigned char *nm = (volatile unsigned char *)&d[12u * c];
#pragma UNROLL(1)
            for (n = 0u; n < 8u; n++) nm[n] = (unsigned char)ak_vname[j][n];
            ch = 1u;
        }
        st->e[c] = st->v[i][j];
    }
    st->drow[0] = st->row[0];
    st->drow[1] = st->row[1];

    /* columns 2..10 (table + record) where they differ */
#pragma UNROLL(1)
    for (c = 2u; c <= 10u; c++) {
        x = st->e[c];
        if (r[c] != x || t[c] != x) {
            t[c] = x;
            r[c] = x;
            ch = 1u;
        }
    }
    return ch;
}

typedef void (*AkWrite)(float *dst, float v, float k);

/* Edit handler of knobs 1, 4, 7, 8, 9 (linker Param.edit; entered from the
 * knob's entry stub with A4 = state, B4 = column). The firmware has just
 * written the turned value into the table row: ak_sync moves a selector
 * one row or puts a locked column back. Then the usual state[7] tail call
 * (params unused). */
#pragma FUNC_CANNOT_INLINE(Ak_MenuEdit)
#pragma CODE_SECTION(Ak_MenuEdit, ".text")
void Ak_MenuEdit(uint32_t *state, uint32_t col)
{
    volatile AkState *st = (volatile AkState *)(uintptr_t)ak_g;
    union { uint32_t u; float f; } k;
    if (st && st->magic0 == AK_MAGIC && st->magic1 == AK_MAGIC && st->lok && st->fwi)
        (void)ak_sync(st, &ak_fwt.row[st->fwi - 1u], st->lslot, 0u);
    k.u = 0x44306666u;
    ((AkWrite)(uintptr_t)state[7])((float *)(uintptr_t)(state[1] + 12u + 4u * col), 0.0f, k.f);
}

/* ---- firmware and target ---- */

/* The running OS's ak_fwt row + 1, or 0. Pure reads (table addresses are
 * aligned RAM on every known OS). */
#pragma FUNC_ALWAYS_INLINE(ak_fwi)
static inline uint32_t ak_fwi(void)
{
    uint32_t k, i, a, v;
#pragma UNROLL(1)
    for (k = 0u; k < AK_NFW; k++) {
        const AkFw *f = &ak_fwt.row[k];
#pragma UNROLL(1)
        for (i = 0u; i < 10u; i++) {
            a = i < 5u ? f->post + 4u * i : f->sig[i - 5u];
            if (!a) break;
            v = i < 3u ? ak_fwt.w[i] : f->val[i - 3u];
            if (AK_U32(a) != v) { i = 0u; break; }
        }
        if (i) return k + 1u;
    }
    return 0u;
}

#pragma FUNC_ALWAYS_INLINE(ak_ddr)
static inline int ak_ddr(uint32_t a)
{
    return a >= 0xC0000000u && a < 0xC8000000u && (a & 3u) == 0u;
}

/* Knob table of slot s, or 0. */
#pragma FUNC_ALWAYS_INLINE(ak_table)
static inline uint32_t ak_table(const AkFw *f, uint32_t s)
{
    uint32_t t = AK_U32(f->info + AK_INFO_STEP * s);
    return ak_ddr(t) ? t : 0u;
}

/* Entry of column col (2..10) in table t if the effect has that knob, or 0:
 * no entry before it may carry the last-entry flag. */
#pragma FUNC_ALWAYS_INLINE(ak_entry)
static inline uint32_t ak_entry(uint32_t t, uint32_t col)
{
    uint32_t c;
#pragma UNROLL(1)
    for (c = 0u; c < col; c++)
        if (AK_U32(t + AK_ENTRY * c + AK_E_FLAGS) & 4u) return 0u;
    return t + AK_ENTRY * col;
}

/* Max of the Knob value for Slot value sv: the target's last knob (its
 * entry with the last-entry flag), or 8 (knob 9) while the slot is Off or
 * holds no usable target (the text shows "---"). */
uint32_t ak_kmax(const AkFw *f, uint32_t sv, uint32_t own)
{
    uint32_t ts = sv - 1u, t, c;
    if (sv == 0u || ts >= f->slots || ts == own || AK_NOGO(AK_U32(f->rec + 44u * ts + 4u)))
        return 8u;
    t = ak_table(f, ts);
    if (!t) return 8u;
#pragma UNROLL(1)
    for (c = 2u; c <= 10u; c++)
        if (AK_U32(t + AK_ENTRY * c + AK_E_FLAGS) & 4u) return c - 2u;
    return 8u;
}

/* Edit handler of knob column col of the effect in slot s (and its knob
 * table in *tab, its max in *mx), or 0: no effect, own slot, no such knob,
 * handler or max out of range, Floppy / AutoKnob, or the slot's ctx doesn't hold the OS's
 * services where AutoKnob replaces them. */
#pragma FUNC_ALWAYS_INLINE(ak_target)
static inline uint32_t ak_target(const AkFw *f, uint32_t s, uint32_t col, uint32_t own,
                                 uint32_t *tab, uint32_t *mx)
{
    uint32_t t, e, h, m, c = AK_CTX0 + AK_CTX_STEP * s;
    *tab = 0u;
    *mx = 0u;
    if (s >= f->slots || s == own || AK_NOGO(AK_U32(f->rec + 44u * s + 4u))) return 0u;
    if (AK_U32(c + 4u * AK_C_SET) != f->fn[0] || AK_U32(c + 4u * AK_C_POST) != f->fn[1] ||
        AK_U32(c + 4u * AK_C_SET9) != f->fn[2] || AK_U32(c + 4u * AK_C_SET10) != f->fn[3] ||
        AK_U32(c + 4u * AK_C_COPY) != f->fn[4] || AK_U32(c + 4u * AK_C_ROUTE) != f->fn[5] ||
        !ak_ddr(AK_U32(c + 4u * AK_C_MEMCPY)))
        return 0u;
    t = ak_table(f, s);
    e = t ? ak_entry(t, col) : 0u;
    if (!e) return 0u;
    h = AK_U32(e + AK_E_EDIT);
    m = AK_U32(e + AK_E_MAX);
    if (!ak_ddr(h) || m == 0u || m > 0xFFFFu) return 0u;
    *tab = t;
    *mx = m;
    return h;
}

/* Show: the record / saved copy of LFO L's target get the knob's own
 * setting and the saved value back. */
#pragma FUNC_ALWAYS_INLINE(ak_unshow)
static inline void ak_unshow(volatile AkLfo *L, const AkFw *f)
{
    if (!L->shown) return;
    *(volatile uint32_t *)(uintptr_t)(f->rec + 44u * L->ts + 4u * L->col) = L->uval;
    *(volatile uint32_t *)(uintptr_t)(f->sav + 44u * L->ts + 4u * L->col) = L->sval;
    L->shown = 0u;
    L->pdirty = 1u;
}

typedef void (*AkPost)(void *event, uint32_t mask);

/* Redraw the knob page if the effect in slot s is the selected one. */
#pragma FUNC_ALWAYS_INLINE(ak_redraw)
static inline void ak_redraw(const AkFw *f, uint32_t s)
{
    if (f->event && *(const volatile uint8_t *)(uintptr_t)f->cursor == s) {
        uint32_t ev = AK_U32(f->event);
        if (ak_ddr(ev))
            ((AkPost)(uintptr_t)f->post)((void *)(uintptr_t)ev, 0x01u);   /* never 0x40 */
    }
}

#pragma FUNC_ALWAYS_INLINE(ak_round)
static inline int32_t ak_round(float x)
{
    return x >= 0.0f ? (int32_t)(x + 0.5f) : -(int32_t)(0.5f - x);
}

/* ---- the LFOs ---- */

/* LFO i, once per cycle: phase, target checks, the next value (handler
 * call at its AK_FIRE call). on = footswitch on. */
#pragma FUNC_CANNOT_INLINE(ak_lfo)
#pragma CODE_SECTION(ak_lfo, ".text")
void ak_lfo(volatile AkState *st, const AkFw *f, uint32_t own, uint32_t i, uint32_t on)
{
    volatile AkLfo *L = &st->l[i];
    volatile const uint32_t *v = st->v[i];
    uint32_t ts, col, id, tab, hand, mx, low, high, rate, wave, show, rv;
    int32_t w;
    float p, q, lfo;

    L->pend = 0u;
    if (!st->lok) return;                  /* menu values not taken yet */
    rate = v[AK_V_RATE];
    wave = v[AK_V_WAVE];
    low = v[AK_V_LOW];
    high = v[AK_V_HIGH];
    show = v[AK_V_SHOW] && f->sav != 0u;

    /* phase: LFO 2 with LFO 1's Rate runs on LFO 1's phase (locked) */
    if (i && rate == st->v[0][AK_V_RATE]) {
        p = st->l[0].phase;
    } else {
        p = L->phase + ak_inc[rate] * (float)AK_GAP;
        if (p >= 1.0f) p -= 1.0f;
    }
    L->phase = p;
    q = p + (float)v[AK_V_PHASE] * (1.0f / 24.0f);
    if (q >= 1.0f) q -= 1.0f;
    if (q < L->lastq) {                    /* a new period: the next S&H value */
        L->rnd = L->rnd * 1664525u + 1013904223u;
        L->hold = (float)(int32_t)(L->rnd >> 9) * (1.0f / 4194304.0f) - 1.0f;
    }
    L->lastq = q;

    /* target: Slot Off = none; LFO 1's knob: LFO 1 wins */
    ts = v[AK_V_SLOT] ? v[AK_V_SLOT] - 1u : AK_NONE;
    col = v[AK_V_KNOB] + 2u;
    if (i && v[AK_V_SLOT] && v[AK_V_SLOT] == st->v[0][AK_V_SLOT] &&
        v[AK_V_KNOB] == st->v[0][AK_V_KNOB])
        ts = AK_NONE;
    id = ts < f->slots ? AK_U32(f->rec + 44u * ts + 4u) : 0u;
    hand = ak_target(f, ts, col, own, &tab, &mx);

    /* target changed: the old one goes back to its setting, but only if its
     * slot still holds the same effect with the same knob table and handler
     * (read again now, never the remembered pointers alone) */
    if (L->ts != ts || L->col != col || L->id != id || L->tab != tab || L->hand != hand) {
        if (L->active && L->stable >= AK_STABLE && L->ts < f->slots &&
            AK_U32(f->rec + 44u * L->ts + 4u) == L->id) {
            uint32_t otab, omx;
            uint32_t oh = ak_target(f, L->ts, L->col, own, &otab, &omx);
            if (oh && oh == L->hand && otab == L->tab) {
                ak_unshow(L, f);
                ak_poke(st, f->tab, L->ts, L->col, oh, L->uval);
            }
        }
        L->shown = 0u;                     /* not restorable: forget it */
        L->ts = ts;
        L->col = col;
        L->id = id;
        L->tab = tab;
        L->hand = hand;
        L->active = 0u;
        L->last = AK_NONE;
        L->stable = 0u;
    }
    if (L->pgap < AK_POST_GAP) L->pgap += AK_GAP;
    if (!hand || L->stable < AK_STABLE) return;

    /* the knob's own setting: the record, unless it holds the LFO's value */
    rv = AK_U32(f->rec + 44u * ts + 4u * col);
    if (!L->shown || rv != L->rlast) L->uval = rv;

    /* Show: redraw the knob page when the shown value changed */
    if (L->pdirty && L->pgap >= AK_POST_GAP) {
        ak_redraw(f, ts);
        L->pdirty = 0u;
        L->pgap = 0u;
    }

    /* off: back to the knob's own setting, once */
    if (!on) {
        if (L->active) {
            ak_unshow(L, f);
            ak_poke(st, f->tab, ts, col, hand, L->uval);
            L->active = 0u;
            L->last = AK_NONE;
        }
        return;
    }
    if (!show && L->shown) ak_unshow(L, f);

    /* LFO -1..1 */
    if (wave == AK_WAVE_SAW) lfo = 1.0f - 2.0f * q;
    else if (wave == AK_WAVE_RAMP) lfo = 2.0f * q - 1.0f;
    else if (wave == AK_WAVE_SQR) lfo = q < 0.5f ? 1.0f : -1.0f;
    else if (wave == AK_WAVE_SH) lfo = L->hold;
    else {
        lfo = q < 0.5f ? 4.0f * q - 1.0f : 3.0f - 4.0f * q;        /* triangle */
        if (wave == AK_WAVE_SINE) lfo = lfo * (1.5f - 0.5f * lfo * lfo);
    }

    /* value = Low .. High (% of the knob's range) */
    w = ak_round(((float)low + (float)((int32_t)high - (int32_t)low) * 0.5f * (lfo + 1.0f))
                 * 0.01f * (float)mx);
    if (w < 0) w = 0;
    if (w > (int32_t)mx) w = (int32_t)mx;

    if ((uint32_t)w != L->last) {
        L->pw = (uint32_t)w;
        L->pshow = show;
        L->pend = 1u;
    }
}

/* LFO i's handler call the cycle prepared (same target: any change since
 * then was a patch change or a chain rebuild, which drop it). */
#pragma FUNC_CANNOT_INLINE(ak_fire)
#pragma CODE_SECTION(ak_fire, ".text")
void ak_fire(volatile AkState *st, const AkFw *f, uint32_t i)
{
    volatile AkLfo *L = &st->l[i];
    uint32_t v = L->pw, ts = L->ts, col = L->col;
    if (!L->pend) return;
    L->pend = 0u;
    ak_poke(st, f->tab, ts, col, L->hand, v);
    if (L->pshow) {
        volatile uint32_t *sv = (volatile uint32_t *)(uintptr_t)(f->sav + 44u * ts + 4u * col);
        if (!L->shown) L->sval = *sv;
        *(volatile uint32_t *)(uintptr_t)(f->rec + 44u * ts + 4u * col) = v;
        *sv = v;
        L->shown = 1u;
        L->rlast = v;
        L->pdirty = 1u;
    }
    L->last = v;
    L->active = 1u;
}

void Fx_SFX_AutoKnob(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *, *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile AkState *st;
    volatile unsigned int *desc;
    const AkFw *f;
    uint32_t own, calm, i, tick;

    *magicDst = *magicSrc;                 /* audio passes through untouched */

    desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;
    {
        uintptr_t base = (uintptr_t)desc[0];
        uintptr_t end = (uintptr_t)desc[1] & ~(uintptr_t)3u;
        if (base == 0u || end <= base + sizeof(AkState)) return;
        st = (volatile AkState *)(end - sizeof(AkState));
    }
    if (st->magic0 != AK_MAGIC || st->magic1 != AK_MAGIC) {
        st->magic0 = AK_MAGIC;
#pragma UNROLL(1)
        for (i = 0u; i < 2u; i++) {
            volatile AkLfo *L = &st->l[i];
            L->ts = AK_NONE;
            L->col = AK_NONE;
            L->id = AK_NONE;
            L->tab = 0u;
            L->hand = 0u;
            L->stable = 0u;
            L->active = 0u;
            L->last = AK_NONE;
            L->rnd = i ? 0x7654321u : 0x1234567u;
            L->phase = 0.25f;              /* Sine / Tri start half way */
            L->lastq = 0.0f;
            L->hold = 0.0f;
            L->uval = 0u;
            L->sval = 0u;
            L->shown = 0u;
            L->rlast = AK_NONE;
            L->pgap = 0u;
            L->pdirty = 0u;
            L->pend = 0u;
        }
        st->patch = AK_NONE;
        st->fwi = 0u;
        st->fwwait = 0u;
        st->tick = 0u;
        st->cyc = 0u;
        st->rtop = 0u;
        st->lok = 0u;
        st->lslot = AK_NONE;
        st->lwait = 0u;
        st->magic1 = AK_MAGIC;
    }

    /* the firmware: found once, then kept */
    if (st->fwi == 0u) {
        if (st->fwwait) {
            st->fwwait--;
            return;
        }
        st->fwi = ak_fwi();
        if (st->fwi == 0u) {
            st->fwwait = AK_FW_WAIT;
            return;
        }
    }
    f = &ak_fwt.row[st->fwi - 1u];
    own = ctx[0];
    if (own >= f->slots) return;
    ak_g = (uint32_t)(uintptr_t)st;

    /* another patch: forget the old targets (their slots may hold other
     * effects now) and the ramps; menu values taken again */
    if (st->patch != AK_U32(f->patch)) {
        st->patch = AK_U32(f->patch);
#pragma UNROLL(1)
        for (i = 0u; i < 2u; i++) {
            st->l[i].ts = AK_NONE;
            st->l[i].active = 0u;
            st->l[i].shown = 0u;           /* the new patch's records are its own */
            st->l[i].pend = 0u;
        }
#pragma UNROLL(1)
        for (i = 0u; i < AK_NRAMP; i++) st->rdst[i] = 0u;
        st->rtop = 0u;
        st->lok = 0u;
        st->lwait = 0u;
    }

    /* The OS is rebuilding the chain (effects moved, added, removed): no
     * handler calls, no ramps (their params may belong to another effect
     * in a moment); a target must then stay unchanged for AK_STABLE calls
     * before AutoKnob calls into it again. */
    calm = AK_U32(f->fdir) == 0u && AK_U32(f->fcopy) == 0u;
    if (!calm) {
#pragma UNROLL(1)
        for (i = 0u; i < 2u; i++) {
            st->l[i].stable = 0u;
            st->l[i].pend = 0u;
        }
#pragma UNROLL(1)
        for (i = 0u; i < AK_NRAMP; i++) st->rdst[i] = 0u;
        st->rtop = 0u;
    } else {
#pragma UNROLL(1)
        for (i = 0u; i < 2u; i++)
            if (st->l[i].stable < AK_STABLE) st->l[i].stable++;
    }

    /* the ramps */
    {
        uint32_t n = st->rtop, top = 0u;
#pragma UNROLL(1)
        for (i = 0u; i < n; i++) {
            volatile float *d = (volatile float *)(uintptr_t)st->rdst[i];
            if (!d) continue;
            if (st->rleft[i] > 1u) {
                *d = *d + st->rstep[i];
                st->rleft[i]--;
            } else {
                *d = st->rtgt[i];
                st->rdst[i] = 0u;
            }
            if (st->rdst[i]) top = i + 1u;
        }
        st->rtop = top;
    }

    /* the cycle */
    tick = ++st->tick;
    if (tick == AK_FIRE0 || tick == AK_FIRE1) {
        ak_fire(st, f, tick == AK_FIRE1);
        return;
    }
    if (tick == AK_LFO2) {
        ak_lfo(st, f, own, 1u, params[0] >= 0.5f);
        return;
    }

    /* the menu (every other cycle): values taken once the rows are calm for
     * AK_LWAIT syncs after a chain rebuild or a move to another slot, then
     * synced */
    if (tick == AK_MENU) {
        uint32_t redraw = 0u;
        st->cyc ^= 1u;
        if (!st->cyc) return;
        if (!calm || st->lslot != own) {
            st->lok = 0u;
            st->lwait = 0u;
            st->lslot = own;
        }
        if (!st->lok) {
            if (calm && ++st->lwait >= AK_LWAIT) {
                redraw = ak_sync(st, f, own, 1u);
                st->lok = 1u;
            }
        } else {
            redraw = ak_sync(st, f, own, 0u);
        }
        if (redraw) ak_redraw(f, own);
        return;
    }
    if (tick < AK_GAP) return;
    st->tick = 0u;
    ak_lfo(st, f, own, 0u, params[0] >= 0.5f);
}

/* ---- knob texts (UI thread; no division) ---- */

/* A printable name of <= 5 characters at a (the firmware's tables). */
#pragma FUNC_ALWAYS_INLINE(ak_name)
static inline int ak_name(uint32_t a, char *s)
{
    int n = 0;
#pragma UNROLL(1)
    while (n < 5) {
        char c = *(const volatile char *)(uintptr_t)(a + (uint32_t)n);
        if (c < ' ' || c > '~') break;
        s[n++] = c;
    }
    if (n == 0) s[n++] = '?';
    s[n] = 0;
    return n;
}

/* A string of <= 7 characters from the own tables. */
#pragma FUNC_CANNOT_INLINE(ak_str)
#pragma CODE_SECTION(ak_str, ".text")
int ak_str(const char *p, char *s)
{
    int n = 0;
#pragma UNROLL(1)
    while (n < 7 && p[n]) { s[n] = p[n]; n++; }
    s[n] = 0;
    return n;
}

static const char ak_waves[AK_NWAVES][8] = { "Sine", "Tri", "Saw", "Ramp", "Sqr", "S&H" };
static const char ak_sdash[] = "---";
static const char ak_soff[] = "Off";
static const char ak_son[] = "On";
static const char ak_sstore[3][8] = { "DON'T", "TURN", "THESE" };

static const uint32_t ak_p10[3] = { 100u, 10u, 1u };

/* v (< 1000) as decimal digits (no division), length returned. */
#pragma FUNC_CANNOT_INLINE(ak_num)
#pragma CODE_SECTION(ak_num, ".text")
int ak_num(uint32_t v, char *s)
{
    int n = 0;
    uint32_t i, d;
    if (v > 999u) v = 999u;
#pragma UNROLL(1)
    for (i = 0u; i < 3u; i++) {
        d = 0u;
#pragma UNROLL(1)
        while (v >= ak_p10[i]) { v -= ak_p10[i]; d++; }
        if (d || n || i == 2u) s[n++] = (char)('0' + d);
    }
    s[n] = 0;
    return n;
}

/* Text of value knob b (0 = A, 1 = B) of LFO i: by the row's value kind. */
#pragma FUNC_CANNOT_INLINE(ak_val)
#pragma CODE_SECTION(ak_val, ".text")
int ak_val(uint32_t i, uint32_t b, uint32_t v, char *s)
{
    volatile AkState *st = (volatile AkState *)(uintptr_t)ak_g;
    const AkFw *f;
    uint32_t k, ts, t, e;
    if (!st || st->magic0 != AK_MAGIC || st->magic1 != AK_MAGIC || !st->lok || !st->fwi)
        return ak_num(v, s);
    f = &ak_fwt.row[st->fwi - 1u];
    k = 2u * st->row[i] + b;
    if (k == AK_V_SLOT || k == AK_V_KNOB) {
        /* Slot: v; Knob: the Slot knob beside it (table row, read live) */
        ts = k == AK_V_SLOT ? v : AK_U32(f->tab + 44u * st->lslot + 4u * AK_ACOL(i));
        if (ts == 0u) return ak_str(k == AK_V_SLOT ? ak_soff : ak_sdash, s);
        if (k == AK_V_KNOB && i && ts == st->v[0][AK_V_SLOT] && v == st->v[0][AK_V_KNOB])
            return ak_str(ak_sdash, s);    /* LFO 1's knob: LFO 1 wins, LFO 2 idles */
        ts--;
        if (ts >= f->slots || ts == st->lslot || AK_NOGO(AK_U32(f->rec + 44u * ts + 4u)))
            return ak_str(ak_sdash, s);
        t = ak_table(f, ts);
        if (!t) return ak_str(ak_sdash, s);
        if (k == AK_V_SLOT) return ak_name(t + AK_ENTRY, s);      /* entry 1 = the effect's name */
        e = v < 9u ? ak_entry(t, v + 2u) : 0u;
        return e ? ak_name(e, s) : ak_str(ak_sdash, s);
    }
    if (k == AK_V_WAVE) return ak_str(v < AK_NWAVES ? ak_waves[v] : ak_sdash, s);
    if (k == AK_V_PHASE) return ak_num(15u * v, s);
    if (k == AK_V_SHOW) return ak_str(v ? ak_son : ak_soff, s);
    return ak_num(v, s);
}

/* Value knobs (not plain tail calls: each needs its own body). */
#pragma CODE_SECTION(GetString_AkVal2, ".text")
int GetString_AkVal2(unsigned int v, char *s)
{
    int n = ak_val(0u, 0u, v, s);
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_AkVal3, ".text")
int GetString_AkVal3(unsigned int v, char *s)
{
    int n = ak_val(0u, 1u, v, s);
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_AkVal5, ".text")
int GetString_AkVal5(unsigned int v, char *s)
{
    int n = ak_val(1u, 0u, v, s);
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_AkVal6, ".text")
int GetString_AkVal6(unsigned int v, char *s)
{
    int n = ak_val(1u, 1u, v, s);
    s[n] = 0;
    return n;
}

/* Selectors: the row's name. */
#pragma CODE_SECTION(GetString_AkRow1, ".text")
int GetString_AkRow1(unsigned int v, char *s)
{
    int n = ak_str(ak_rname[v >> AK_SSH(0) & (AK_NROWS - 1u)], s);
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_AkRow4, ".text")
int GetString_AkRow4(unsigned int v, char *s)
{
    int n = ak_str(ak_rname[v >> AK_SSH(1) & (AK_NROWS - 1u)], s);
    s[n] = 0;
    return n;
}

/* Storage knobs 7-9 (names Data1..Data3): DON'T / TURN / THESE (each knob
 * needs its own body). */
#pragma CODE_SECTION(GetString_AkStore7, ".text")
int GetString_AkStore7(unsigned int v, char *s)
{
    int n = ak_str(ak_sstore[0], s);
    (void)v;
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_AkStore8, ".text")
int GetString_AkStore8(unsigned int v, char *s)
{
    int n = ak_str(ak_sstore[1], s);
    (void)v;
    s[n] = 0;
    return n;
}

#pragma CODE_SECTION(GetString_AkStore9, ".text")
int GetString_AkStore9(unsigned int v, char *s)
{
    int n = ak_str(ak_sstore[2], s);
    (void)v;
    s[n] = 0;
    return n;
}
