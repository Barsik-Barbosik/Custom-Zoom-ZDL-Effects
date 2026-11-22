/*
 * AutoKnob -- an LFO that moves a knob of another effect in the chain,
 * stock effects included. No sound of its own (audio passes through).
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
 * written: the target knob goes back to that value when AutoKnob stops.
 *
 * Firmware: ak_fwt holds the known OSes (G1on family 1.21: HW-tested;
 * MS-70CDR 2.10, MS-50G 3.10, MS-60B 2.10: addresses from the firmware
 * images, untested). Before any handler call the target's ctx words 7, 8,
 * 9, 10, 15 and 32 must hold exactly that OS's functions; on an unknown
 * OS, or a mismatch, AutoKnob does nothing.
 *
 * Knobs: Slot (target chain position, text = target effect name, max = the
 * OS's last position), Knob (target knob, text = its name, max = the
 * target's last knob: set at run time), Wave (Sine, Tri, Saw, Ramp, Sqr,
 * S&H); Rate (0..100, 0.05..10 Hz), Low and High (0..100 % of the knob's
 * range: the LFO sweeps between them; Low > High sweeps upside down); Show
 * (Off / On). Footswitch off or a target change: the old target knob goes
 * back to its own setting. Never a target: Floppy (its knobs hold saved
 * data) and AutoKnob itself.
 *
 * Show On (OSes with a known saved-patch copy only; MS rows: static, untested):
 * the LFO value also goes into the target's patch record, and the knob page
 * is redrawn (Event_post, ~30 / s while the target is the selected effect),
 * so the page follows the LFO. The same value goes into the saved-patch
 * copy, so the patch doesn't count as edited (no EDITED, no Auto Save). The
 * knob's own setting (uval) and the saved copy's value (sval) are kept and
 * written back when Show goes off, the footswitch goes off or the target
 * changes; a record value AutoKnob didn't write is a knob turn: the new
 * setting. A patch saved while Show is on keeps the LFO value of that
 * moment.
 *
 * CPU: every call only checks the patch number and the chain-rebuild flags
 * and runs the ramps; the knobs, the target checks and the LFO run once
 * every AK_GAP calls (a tick), the handler call for the new value AK_HALF
 * calls later (each part reads tables the other effects have pushed out
 * of the cache, so apart they peak lower). The firmware row is
 * found once and kept in the state (unknown firmware: looked for again
 * every AK_FW_WAIT calls).
 */

#include <stdint.h>

#include "autoknob_rates.h"

#pragma CODE_SECTION(Fx_SFX_AutoKnb1, ".audio")

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))
#define AK_U32(a) (*(const volatile uint32_t *)(uintptr_t)(a))

#define AK_ID        ((7u << 24) | 523u)  /* own record word +4 */
#define AK_FLOPPY    ((7u << 24) | 522u)  /* Floppy: knobs = stored data, never moved */
#define AK_AK2       ((7u << 24) | 524u)  /* AutoKnob 2 */
#define AK_NOGO(id)  ((id) == 0u || (id) == AK_ID || (id) == AK_AK2 || (id) == AK_FLOPPY)
#define AK_CTX0      0x11F03000u      /* per-slot ctx, 212 B (53 words) each (all OSes) */
#define AK_CTX_STEP  212u
#define AK_CTX_WORDS 53u
#define AK_INFO_STEP 28u              /* per-slot info; word 0 = knob table */
#define AK_ENTRY     48u              /* knob table entry */
#define AK_E_MAX     0x0Cu            /* entry: max value */
#define AK_E_EDIT    0x1Cu            /* entry: edit handler */
#define AK_E_FLAGS   0x2Cu            /* entry: flags, 0x04 = last entry */
#define AK_MAGIC     0x414B4E34u      /* "AKN4"; new value on any state layout change */
#define AK_POST_GAP  184u             /* Show: calls between redraws (~30 / s) */
#define AK_FW_WAIT   4096u            /* unknown firmware: calls between looks */
#define AK_NONE      0xFFFFFFFFu
#define AK_GAP       32u              /* calls per tick = between handler calls (~172 Hz) */
#define AK_HALF      16u              /* tick -> its handler call */
#define AK_LIMIT     8u               /* tick -> the knob max update (own call) */
#define AK_DESC      _Fx_SFX_AutoKnb1_Desc
#define AK_INV_GAP   (1.0f / 32.0f)
#define AK_NRAMP     16u              /* params ramping at once */
#define AK_AREA      0x400u           /* only params within this many bytes of the
                                       * target's ctx[1] / ctx[2] ramp */
#define AK_MAXSLOTS  6u
#define AK_STABLE    512u             /* calls the target must stay unchanged (~93 ms) */

#define AK_WAVE_SINE 0
#define AK_WAVE_TRI  1
#define AK_WAVE_SAW  2
#define AK_WAVE_RAMP 3
#define AK_WAVE_SQR  4
#define AK_WAVE_SH   5
#define AK_NWAVES    6

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

/* State at the ctx[3] arena's end, magic at both ends. */
typedef struct {
    uint32_t magic0;
    uint32_t copy[AK_CTX_WORDS];      /* the borrowed ctx */
    uint32_t ts, col, id;             /* current target (AK_NONE = none) */
    uint32_t tab, hand;               /* its knob table and edit handler */
    uint32_t stable;                  /* calls the target has stayed unchanged */
    uint32_t active;                  /* the target's table holds an LFO value */
    uint32_t last;                    /* value last written */
    uint32_t rnd;                     /* S&H generator */
    float phase, hold;                /* LFO phase 0..1, S&H value */
    uint32_t patch;                   /* patch number the fields belong to */
    uint32_t uval;                    /* the target knob's own setting */
    uint32_t sval;                    /* Show: the saved copy's value before */
    uint32_t shown;                   /* Show: record / saved copy hold rlast */
    uint32_t rlast;                   /* Show: value last written there */
    uint32_t pgap, pdirty;            /* Show: calls since the last redraw, redraw due */
    uint32_t fwi;                     /* ak_fwt row + 1 (0 = not found yet) */
    uint32_t fwwait;                  /* calls until the next look (unknown firmware) */
    uint32_t tick;                    /* calls since the last tick */
    uint32_t rtop;                    /* ramps in use are below this index */
    uint32_t pend, pw, pshow;         /* handler call due at AK_HALF: value, Show */
    uint32_t kslot;                   /* Slot value the Knob max was set for */
    uint32_t rdst[AK_NRAMP];          /* ramping params (0 = free) */
    uint32_t rleft[AK_NRAMP];         /* calls left */
    float rtgt[AK_NRAMP], rstep[AK_NRAMP];
    uint32_t magic1;
} AkState;

/* The state during a handler call, for ak_ramp (the 4-byte .fardata). */
static volatile uint32_t ak_g = 0u;

/* The own descriptor (linker): entry = column, 12 words, word 3 = max. */
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
    ak_g = 0u;
}

/* The running OS's ak_fwt row + 1, or 0. Pure reads (table addresses are
 * aligned RAM on every known OS). */
#pragma FUNC_CANNOT_INLINE(ak_fwi)
#pragma CODE_SECTION(ak_fwi, ".text")
#pragma FUNCTION_OPTIONS(ak_fwi, "--opt_for_space=3")
uint32_t ak_fwi(void)
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

/* The running OS's ak_fwt row, or 0. */
#pragma FUNC_ALWAYS_INLINE(ak_fw)
static inline const AkFw *ak_fw(void)
{
    uint32_t i = ak_fwi();
    return i ? &ak_fwt.row[i - 1u] : 0;
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

/* Show: the record / saved copy of the current target get the knob's own
 * setting and the saved value back. */
#pragma FUNC_ALWAYS_INLINE(ak_unshow)
static inline void ak_unshow(volatile AkState *st, const AkFw *f)
{
    if (!st->shown) return;
    *(volatile uint32_t *)(uintptr_t)(f->rec + 44u * st->ts + 4u * st->col) = st->uval;
    *(volatile uint32_t *)(uintptr_t)(f->sav + 44u * st->ts + 4u * st->col) = st->sval;
    st->shown = 0u;
    st->pdirty = 1u;
}

typedef void (*AkPost)(void *event, uint32_t mask);

/* Max of the Knob value for target slot ts: the target's last knob (its
 * entry with the last-entry flag), or 8 (knob 9) while the slot holds no
 * usable target (the text shows "---"). */
#pragma FUNC_ALWAYS_INLINE(ak_kmax)
static inline uint32_t ak_kmax(const AkFw *f, uint32_t ts, uint32_t own)
{
    uint32_t t, c;
    if (ts >= f->slots || ts == own || AK_NOGO(AK_U32(f->rec + 44u * ts + 4u))) return 8u;
    t = ak_table(f, ts);
    if (!t) return 8u;
#pragma UNROLL(1)
    for (c = 2u; c <= 10u; c++)
        if (AK_U32(t + AK_ENTRY * c + AK_E_FLAGS) & 4u) return c - 2u;
    return 8u;
}

/* Run-time max of Slot (the OS's last chain position) and Knob (the
 * target's last knob) in the own descriptor, so no value past the end shows
 * "---". After a Slot turn also Knob down to the new max (table + record;
 * not before the first Slot value of a patch is known: the targets may still
 * be loading). Anything written: the knob page is redrawn if AutoKnob is the
 * selected effect. */
#pragma FUNC_CANNOT_INLINE(ak_limits)
#pragma CODE_SECTION(ak_limits, ".text")
#pragma FUNCTION_OPTIONS(ak_limits, "--opt_for_space=3")
void ak_limits(volatile AkState *st, const AkFw *f, uint32_t own)
{
    volatile unsigned int *d = (volatile unsigned int *)AK_DESC;
    volatile uint32_t *t = (volatile uint32_t *)(uintptr_t)(f->tab + 44u * own);
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(f->rec + 44u * own);
    uint32_t ts = t[2], m, ch = 0u;
    m = f->slots - 1u;
    if (d[12u * 2u + 3u] != m) {
        d[12u * 2u + 3u] = m;
        ch = 1u;
    }
    m = ak_kmax(f, ts, own);
    if (d[12u * 3u + 3u] != m) {
        d[12u * 3u + 3u] = m;
        ch = 1u;
    }
    if (ts != st->kslot) {
        if (st->kslot != AK_NONE && t[3] > m) {
            t[3] = m;
            r[3] = m;
            ch = 1u;
        }
        st->kslot = ts;
    }
    if (ch && f->event && *(const volatile uint8_t *)(uintptr_t)f->cursor == own) {
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

void Fx_SFX_AutoKnb1(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *, *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    volatile AkState *st;
    volatile unsigned int *desc;
    const AkFw *f;
    uint32_t ts, col, id, tab, hand, mx, low, high, rate, wave, calm, show, rv;
    int32_t w;
    float p, lfo;

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
        st->ts = AK_NONE;
        st->col = AK_NONE;
        st->id = AK_NONE;
        st->tab = 0u;
        st->hand = 0u;
        st->stable = 0u;
        st->active = 0u;
        st->last = AK_NONE;
        st->rnd = 0x1234567u;
        st->phase = 0.25f;                 /* Sine / Tri start half way */
        st->hold = 0.0f;
        st->patch = AK_NONE;
        st->uval = 0u;
        st->sval = 0u;
        st->shown = 0u;
        st->rlast = AK_NONE;
        st->pgap = 0u;
        st->pdirty = 0u;
        st->fwi = 0u;
        st->fwwait = 0u;
        st->tick = 0u;
        st->rtop = 0u;
        st->pend = 0u;
        st->kslot = AK_NONE;
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
    if (ctx[0] >= f->slots) return;

    /* another patch: forget the old target (its slot may hold another
     * effect now) and the ramps */
    if (st->patch != AK_U32(f->patch)) {
        uint32_t i;
        st->patch = AK_U32(f->patch);
        st->ts = AK_NONE;
        st->active = 0u;
        st->shown = 0u;                    /* the new patch's records are its own */
#pragma UNROLL(1)
        for (i = 0u; i < AK_NRAMP; i++) st->rdst[i] = 0u;
        st->rtop = 0u;
        st->pend = 0u;
        st->kslot = AK_NONE;
    }

    /* The OS is rebuilding the chain (effects moved, added, removed): no
     * handler calls, no ramps (their params may belong to another effect
     * in a moment); the target must then stay unchanged for AK_STABLE calls
     * before AutoKnob calls into it again. */
    calm = AK_U32(f->fdir) == 0u && AK_U32(f->fcopy) == 0u;
    if (!calm) {
        uint32_t i;
        st->stable = 0u;
#pragma UNROLL(1)
        for (i = 0u; i < AK_NRAMP; i++) st->rdst[i] = 0u;
        st->rtop = 0u;
        st->pend = 0u;
    } else if (st->stable < AK_STABLE) {
        st->stable++;
    }

    /* the ramps */
    {
        uint32_t i, n = st->rtop, top = 0u;
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

    /* the handler call the last tick prepared (same target: any change
     * since then was a patch change or a chain rebuild, which drop it) */
    if (++st->tick == AK_HALF) {
        if (st->pend) {
            uint32_t v = st->pw, ts0 = st->ts, col0 = st->col;
            st->pend = 0u;
            ak_poke(st, f->tab, ts0, col0, st->hand, v);
            if (st->pshow) {
                volatile uint32_t *sv = (volatile uint32_t *)(uintptr_t)(f->sav + 44u * ts0 + 4u * col0);
                if (!st->shown) st->sval = *sv;
                *(volatile uint32_t *)(uintptr_t)(f->rec + 44u * ts0 + 4u * col0) = v;
                *sv = v;
                st->shown = 1u;
                st->rlast = v;
                st->pdirty = 1u;
            }
            st->last = v;
            st->active = 1u;
        }
        return;
    }

    /* the Slot / Knob max (own call; once the target has been calm) */
    if (st->tick == AK_LIMIT) {
        if (calm && st->stable >= AK_STABLE) ak_limits(st, f, ctx[0]);
        return;
    }

    /* the rest once per tick */
    if (st->tick < AK_GAP) return;
    st->tick = 0u;
    st->pend = 0u;

    /* knobs (raw = value / 100; Knob from the table row, which ak_limits
     * may lower) */
    ts = (uint32_t)ak_round(params[5] * 100.0f);
    col = AK_U32(f->tab + 44u * ctx[0] + 12u) + 2u;
    wave = (uint32_t)ak_round(params[7] * 100.0f);
    rate = (uint32_t)ak_round(params[8] * 100.0f);
    low = (uint32_t)ak_round(params[9] * 100.0f);
    high = (uint32_t)ak_round(params[10] * 100.0f);
    show = params[11] > 0.005f && f->sav != 0u;
    if (ts > AK_MAXSLOTS - 1u) ts = AK_MAXSLOTS - 1u;
    if (col > 10u) col = 10u;
    if (low > 100u) low = 100u;
    if (high > 100u) high = 100u;
    if (rate > AK_NRATES - 1u) rate = AK_NRATES - 1u;
    if (wave > AK_NWAVES - 1u) wave = AK_NWAVES - 1u;

    /* target: another effect's knob with a handler in DDR */
    id = ts < f->slots ? AK_U32(f->rec + 44u * ts + 4u) : 0u;
    hand = ak_target(f, ts, col, ctx[0], &tab, &mx);

    /* target changed: the old one goes back to its setting, but only if its
     * slot still holds the same effect with the same knob table and handler
     * (read again now, never the remembered pointers alone) */
    if (st->ts != ts || st->col != col || st->id != id || st->tab != tab || st->hand != hand) {
        if (st->active && calm && st->stable >= AK_STABLE && st->ts < f->slots &&
            AK_U32(f->rec + 44u * st->ts + 4u) == st->id) {
            uint32_t otab, omx;
            uint32_t oh = ak_target(f, st->ts, st->col, ctx[0], &otab, &omx);
            if (oh && oh == st->hand && otab == st->tab) {
                ak_unshow(st, f);
                ak_poke(st, f->tab, st->ts, st->col, oh, st->uval);
            }
        }
        st->shown = 0u;                    /* not restorable: forget it */
        st->ts = ts;
        st->col = col;
        st->id = id;
        st->tab = tab;
        st->hand = hand;
        st->active = 0u;
        st->last = AK_NONE;
        st->stable = 0u;
    }
    if (st->pgap < AK_POST_GAP) st->pgap += AK_GAP;
    if (!hand || st->stable < AK_STABLE) return;

    /* the knob's own setting: the record, unless it holds AutoKnob's value */
    rv = AK_U32(f->rec + 44u * ts + 4u * col);
    if (!st->shown || rv != st->rlast) st->uval = rv;

    /* Show: redraw the knob page when the shown value changed and the
     * target is the selected effect */
    if (st->pdirty && st->pgap >= AK_POST_GAP && f->event &&
        *(const volatile uint8_t *)(uintptr_t)f->cursor == ts) {
        uint32_t ev = AK_U32(f->event);
        if (ev >= 0xC0000000u && ev < 0xC8000000u && (ev & 3u) == 0u)
            ((AkPost)(uintptr_t)f->post)((void *)(uintptr_t)ev, 0x01u);   /* never 0x40 */
        st->pdirty = 0u;
        st->pgap = 0u;
    }

    /* off: back to the knob's own setting, once */
    if (params[0] < 0.5f) {
        if (st->active) {
            ak_unshow(st, f);
            ak_poke(st, f->tab, ts, col, hand, st->uval);
            st->active = 0u;
            st->last = AK_NONE;
        }
        return;
    }
    if (!show && st->shown) ak_unshow(st, f);

    /* LFO -1..1 */
    p = st->phase + ak_inc[rate] * (float)AK_GAP;
    if (p >= 1.0f) {
        p -= 1.0f;
        st->rnd = st->rnd * 1664525u + 1013904223u;
        st->hold = (float)(int32_t)(st->rnd >> 9) * (1.0f / 4194304.0f) - 1.0f;
    }
    st->phase = p;
    if (wave == AK_WAVE_SAW) lfo = 1.0f - 2.0f * p;
    else if (wave == AK_WAVE_RAMP) lfo = 2.0f * p - 1.0f;
    else if (wave == AK_WAVE_SQR) lfo = p < 0.5f ? 1.0f : -1.0f;
    else if (wave == AK_WAVE_SH) lfo = st->hold;
    else {
        lfo = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;        /* triangle */
        if (wave == AK_WAVE_SINE) lfo = lfo * (1.5f - 0.5f * lfo * lfo);
    }

    /* value = Low .. High (% of the knob's range) */
    w = ak_round(((float)low + (float)((int32_t)high - (int32_t)low) * 0.5f * (lfo + 1.0f))
                 * 0.01f * (float)mx);
    if (w < 0) w = 0;
    if (w > (int32_t)mx) w = (int32_t)mx;

    if ((uint32_t)w != st->last) {          /* handler call AK_HALF calls later */
        st->pw = (uint32_t)w;
        st->pshow = show;
        st->pend = 1u;
    }
}

/* ---- knob texts (UI thread; no calls, no division) ---- */

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

#pragma FUNC_CANNOT_INLINE(ak_dash)
#pragma CODE_SECTION(ak_dash, ".text")
#pragma FUNCTION_OPTIONS(ak_dash, "--opt_for_space=3")
int ak_dash(char *s)
{
    s[0] = '-';
    s[1] = '-';
    s[2] = '-';
    s[3] = 0;
    return 3;
}

/* Slot: the name of the effect in that chain position. */
#pragma CODE_SECTION(GetString_AkSlot, ".text")
#pragma FUNCTION_OPTIONS(GetString_AkSlot, "--opt_for_space=3")
int GetString_AkSlot(unsigned int v, char *s)
{
    uint32_t id, t;
    const AkFw *f = ak_fw();
    if (!f || v >= f->slots) return ak_dash(s);
    id = AK_U32(f->rec + 44u * v + 4u);
    t = ak_table(f, v);
    if (AK_NOGO(id) || !t) return ak_dash(s);
    return ak_name(t + AK_ENTRY, s);           /* entry 1 = the effect's name */
}

/* Knob: the name of that knob of the target (own Slot column from the
 * own patch record; the first AutoKnob in the chain). */
#pragma CODE_SECTION(GetString_AkKnob, ".text")
#pragma FUNCTION_OPTIONS(GetString_AkKnob, "--opt_for_space=3")
int GetString_AkKnob(unsigned int v, char *s)
{
    uint32_t own, ts, t, e;
    const AkFw *f = ak_fw();
    if (!f || v > 8u) return ak_dash(s);
#pragma UNROLL(1)
    for (own = 0u; own < f->slots; own++)
        if (AK_U32(f->rec + 44u * own + 4u) == AK_ID) break;
    if (own >= f->slots) return ak_dash(s);
    ts = AK_U32(f->rec + 44u * own + 8u);       /* own knob 1 = Slot */
    if (ts >= f->slots || ts == own || AK_NOGO(AK_U32(f->rec + 44u * ts + 4u))) return ak_dash(s);
    t = ak_table(f, ts);
    e = t ? ak_entry(t, v + 2u) : 0u;
    return e ? ak_name(e, s) : ak_dash(s);
}

#pragma CODE_SECTION(GetString_AkShow, ".text")
#pragma FUNCTION_OPTIONS(GetString_AkShow, "--opt_for_space=3")
int GetString_AkShow(unsigned int v, char *s)
{
    if (v) { s[0] = 'O'; s[1] = 'n'; s[2] = 0; return 2; }
    s[0] = 'O'; s[1] = 'f'; s[2] = 'f'; s[3] = 0;
    return 3;
}

static const char ak_waves[AK_NWAVES][5] = { "Sine", "Tri", "Saw", "Ramp", "Sqr", "S&H" };

#pragma CODE_SECTION(GetString_AkWave, ".text")
#pragma FUNCTION_OPTIONS(GetString_AkWave, "--opt_for_space=3")
int GetString_AkWave(unsigned int v, char *s)
{
    int n = 0;
    if (v >= AK_NWAVES) return ak_dash(s);
#pragma UNROLL(1)
    while (n < 4 && ak_waves[v][n]) { s[n] = ak_waves[v][n]; n++; }
    s[n] = 0;
    return n;
}
