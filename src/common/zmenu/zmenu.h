/* zmenu: knob menus whose values are saved in the patch through the Floppy
 * companion effect (G1on family 1.21 / B1Xon 1.30, tested; MS-70CDR 2.10,
 * MS-50G 3.10, MS-60B 2.10, untested).
 *
 * A menu is one selector knob plus the field knobs right after it. The
 * selector picks a record (a bar, a parameter); the field knobs edit that
 * record's values and jump to them when the selector moves (their own patch
 * columns and params[] are rewritten, then a redraw is posted). A parameter
 * menu has one field knob whose name follows the selector (the descriptor
 * name is rewritten).
 *
 * Every value lives in a slot (record x field). Slots are packed into the
 * 9 knob fields of the Floppy effect (another chain slot, found every call by
 * its record word +4) as one bit stream; a value may span fields. Stored
 * value = (value - default) mod 2^bits, so a fresh Floppy (all zero) reads
 * as the defaults. Mixed-radix packing (ZM_RADIX 1, zmenu_gen.py
 * packing="radix") stores all slots as one number instead, slot i as the
 * digit (value - default) mod steps_i, slot 0 least significant: a value
 * costs log2(steps) bits, not a whole number of them. The number lives in
 * 16-bit limbs (z->big); dividing by a radix <= 256 works limb by limb on
 * values < 2^24, so a float multiply by 1/steps plus one correction gives
 * the exact quotient (no integer divide). Every value change re-encodes
 * the whole number. Without Floppy, or on an unknown firmware, values stay in
 * the effect's state only (not saved) and the selector text shows "--".
 *
 * Usage: zmenu_gen.py writes <effect>_zmenu.h (layout tables, the getstr
 * callbacks GetString_Zm1.. and ZM_SLOT_* macros). The effect defines
 * ZM_DESC (its descriptor table symbol, `_<audio func>_Desc`), includes that
 * header, keeps a ZmState in its ctx[3] state, calls zm_poll(&zm, ctx) at the
 * top of every audio call and reads values with zm_val(&zm, slot).
 *
 * Rules: force-inlined helpers only (no calls between sections), no stack
 * arrays, no integer divide. OS addresses are read or called only after the
 * fingerprint of zm_fws[] matched (re-checked every call: the state lives in
 * ctx[3], which is lent out across patch switches). Firmware table after
 * zvis.h (themanro's, with the G1on entry). */
#ifndef ZMENU_H
#define ZMENU_H
#include <stdint.h>

/* Stream mode (ZM_STREAM 1, defined by the effect before including this
 * file, with ZM_DESC, ZM_MAGIC and ZM_FLOPPY_FXID): no menu layout. The
 * effect runs its own menu and packs its own data; zmenu finds Floppy and
 * moves its 106 bits (G1on widths) in and out (zm_s_poll / zm_s_get /
 * zm_s_put), jumps own knobs (zm_s_jump) and posts the redraws (zm_s_post).
 * More copies of the same Floppy give more bits (up to ZM_S_PARTS parts):
 * part 1 is unmarked (its first 4 bits are the effect's, which must keep
 * them below 12), each extra copy carries a marker there (12 = part 2,
 * 13 = part 3, 14 = part 4) and 102 bits of its own after it. Of the unmarked
 * copies the first non-empty one in chain order is part 1 (else the first);
 * the others get the lowest free marker (and zeros) as soon as they are
 * found, so the copies may be reordered afterwards.
 * MS pedals: the same 106 bits assumed (their field widths are untested). */
#if defined(ZM_STREAM) && ZM_STREAM
#define ZM_IS_STREAM 1
#if !defined(ZM_DESC) || !defined(ZM_MAGIC) || !defined(ZM_FLOPPY_FXID)
#error "stream mode: define ZM_DESC, ZM_MAGIC and ZM_FLOPPY_FXID"
#endif
#else
#define ZM_IS_STREAM 0
#if !defined(ZM_NKNOB) || !defined(ZM_NMENU) || !defined(ZM_NSLOT) || !defined(ZM_DESC)
#error "include the generated <effect>_zmenu.h (which includes zmenu.h), with ZM_DESC defined"
#endif
#endif

#define ZM_DO_PRAGMA(x) _Pragma(#x)
#define ZM_EXPAND_PRAGMA(x) ZM_DO_PRAGMA(x)
#define ZM_INLINE(f) ZM_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(f))
#define ZM_U32(a) (*(const volatile uint32_t *)(uintptr_t)(a))

#define ZM_FW_MS70  1                   /* zm_fw_ok(): index into zm_fws[] + 1 */
#define ZM_FW_G1ON  2
#define ZM_FW_MS50  3
#define ZM_FW_MS60  4
#define ZM_NFW      4u
#define ZM_NOSLOT   0xFFu               /* Floppy not found */
#define ZM_NOCOL    0xFFFFFFFFu         /* column cache invalid */
#define ZM_SEL      0xFFu               /* zm_text field index of a selector */
#define ZM_NONAMES  0xFFFFu
#define ZM_POSTS    3u                  /* redraw posts per selector change */
#define ZM_POST_GAP 64u                 /* calls between them (~12 ms) */

/* One known OS (zvis.h's ZvFw plus the slot rows and the Floppy id test).
 * sig: fingerprint (address, value), address 0 ends; sig[0][0] = Event_post.
 * event: where the UI event object pointer is kept. Screen test: zero[]
 * must read 0, nonzero not (0 = unused), page = page word (0 = none),
 * cursor = selected effect byte, map = slot map (mstride B per page, 4 B
 * per cursor position, 0 = cursor is the slot). rec / tab: patch record rows
 * and state[31] table rows, 44 B per slot (col 0 On/Off, 1 type, 2..10 knobs
 * 1..9). A slot holds Floppy when (rec word +4 & idmask) == idval. patch:
 * the current patch number word (not used by zmenu; for effects that track
 * patch changes). */
typedef struct {
    uint32_t sig[10][2];
    uint32_t event;
    uint32_t zero[3], nonzero, page, cursor, map, mstride;
    uint32_t rec, tab, slots, idmask, idval, patch;
} ZmFw;

static const ZmFw zm_fws[ZM_NFW] = {
    /* MS-70CDR 2.10 (zvis.h's entry; rows from the state[31] lookup at
     * 0xC00B820C, static; record word +4 = the ZDL header's u32 at 0x40,
     * (gid << 24) | fxid as on the G1on, compared whole by the OS).
     * UNTESTED: no slot-row write or Floppy test on an MS pedal yet. */
    { { { 0xC00CBF40u, 0x27773577u }, { 0xC00CBF44u, 0x85778677u }, { 0xC00CBF48u, 0xB64631F7u },
        { 0xC00CBF4Cu, 0x02E7442Bu }, { 0xC00CBF50u, 0x01E74228u }, { 0xC00BB168u, 0x1021BC13u },
        { 0xC00BB16Cu, 0x0200A46Du }, { 0xC00DD818u, 0x077BA42Au }, { 0xC00DD81Cu, 0x0760076Au },
        { 0xC00BADA8u, 0x033CFFA9u } },
      0xC00EF9D8u,
      { 0xC00EF888u, 0xC00EF9A8u, 0xC009D398u }, 0xC00EF894u, 0xC009D8F8u, 0xC009C064u, 0xC00EE748u, 24u,
      0xC009BF58u, 0xC009C1A0u, 6u, 0xFFFFFFFFu, (7u << 24) | ZM_FLOPPY_FXID, 0xC00EF88Cu },
    /* G1on family (G1on / G1Xon 1.21, B1on / B1Xon 1.30, one OS image).
     * HW (B1Xon 1.30): rows, jumps, Floppy storage (MenuPrb stages 2-14). */
    { { { 0xC00B05C0u, 0x27773577u }, { 0xC00B05C4u, 0x85778677u }, { 0xC00B05C8u, 0xB64631F7u },
        { 0xC00B05CCu, 0x02C53A2Bu }, { 0xC00B05D0u, 0x01C50628u }, { 0xC009B278u, 0x0200736Du },
        { 0xC009B280u, 0x10089013u }, { 0xC008B318u, 0x02C65A28u }, { 0xC008B320u, 0x02104CA0u },
        { 0u, 0u } },
      0xC00DEBCCu,
      { 0u, 0u, 0u }, 0u, 0u, 0xC0078CB4u, 0u, 0u,
      0x1181B160u, 0x1181B370u, 5u, 0xFFFFFFFFu, (7u << 24) | ZM_FLOPPY_FXID, 0xC00DEADCu },
    /* MS-50G 3.10 and MS-60B 2.10: the MS-70CDR entry's addresses found again
     * in their OS (static, by matching the code that uses each one; B14 =
     * 0xC00EFCC0 / 0xC00DFCF0). The MS-50G keeps the MS-70CDR's rows, cursor,
     * page and map stride; the MS-60B has 4 slots and 16 B per map page.
     * UNTESTED on both pedals. */
    { { { 0xC00CC680u, 0x27773577u }, { 0xC00CC684u, 0x85778677u }, { 0xC00CC688u, 0xB64631F7u },
        { 0xC00CC68Cu, 0x02E9C02Bu }, { 0xC00CC690u, 0x01E9BE28u }, { 0xC00AB784u, 0x1041E013u },
        { 0xC00AB788u, 0x0200A96Cu }, { 0xC00DDCD8u, 0x077E602Au }, { 0xC00DDCDCu, 0x0760076Au },
        { 0xC00BA650u, 0x033CFFA9u } },
      0xC00EFF64u,
      { 0xC00EFE08u, 0xC00EFF2Cu, 0xC009D398u }, 0xC00EFE14u, 0xC009D8F8u, 0xC009C064u, 0xC00EECB0u, 24u,
      0xC009BF58u, 0xC009C1A0u, 6u, 0xFFFFFFFFu, (7u << 24) | ZM_FLOPPY_FXID, 0xC00EFE0Cu },
    { { { 0xC00BC500u, 0x27773577u }, { 0xC00BC504u, 0x85778677u }, { 0xC00BC508u, 0xB64631F7u },
        { 0xC00BC50Cu, 0x02EA982Bu }, { 0xC00BC510u, 0x01EA9628u }, { 0xC009B5E4u, 0x1041E413u },
        { 0xC009B5E8u, 0x0200A66Cu }, { 0xC00CDEB8u, 0x077E782Au }, { 0xC00CDEBCu, 0x076006EAu },
        { 0xC00AA664u, 0x033CFFA9u } },
      0xC00DFF88u,
      { 0xC00DFE38u, 0xC00DFF54u, 0xC008D230u }, 0xC00DFE44u, 0xC008D644u, 0xC008C754u, 0xC00DEE58u, 16u,
      0xC008C6A0u, 0xC008C838u, 4u, 0xFFFFFFFFu, (7u << 24) | ZM_FLOPPY_FXID, 0xC00DFE3Cu },
};

#if !ZM_IS_STREAM
/* Layout, from the generated header. A menu: selector knob `sel` (0-based),
 * field knobs sel + 1 .. sel + nfld; nrec records; slots slot0 + r * nfld + f;
 * rename: the Value knob's name follows the record (selector names). A slot's
 * value is split into up to 3 parts: bits n of Floppy column 2 + col,
 * starting at bit fsh, holding value bits vsh.. */
typedef struct { uint8_t sel, nfld, nrec, rename; uint16_t slot0, names; } ZmMenu;
#if ZM_RADIX
/* Mixed radix: steps = max + 1, inv = 1 / steps. A Floppy column holds n
 * bits of the number from bit start on. */
typedef struct { uint16_t def, max, names, pad; float inv; } ZmSlot;
typedef struct { uint8_t start, n; } ZmCol;
static const ZmCol zm_cols[9];
#else
typedef struct { uint8_t col, fsh, n, vsh; } ZmPart;
typedef struct { uint16_t def, max, names, bits; ZmPart part[3]; } ZmSlot;
#endif

static const ZmMenu zm_menus[ZM_NMENU];
static const ZmSlot zm_slots[ZM_NSLOT];
static const uint16_t zm_kmax[ZM_NKNOB];        /* descriptor max per menu knob */
static const char zm_names[ZM_NNAMES * 5 + 1];  /* 5-char names, NUL-padded */

typedef struct {
    uint32_t magic;
    uint32_t slot, fw, fslot;           /* own slot, ZM_FW_*, Floppy's slot */
    uint32_t posts, postWait;
    uint32_t fcol[9];                   /* Floppy's columns as last decoded */
    uint16_t last[ZM_NKNOB];            /* knob values as last seen */
    uint8_t  sel[ZM_NMENU];             /* current record per menu */
    uint16_t vals[ZM_NSLOT];
#if ZM_RADIX
    uint32_t big[ZM_NLIMB];             /* the number, 16 bits per limb */
#endif
} ZmState;

/* Shared with the knob text callbacks (they get no ctx). */
static volatile ZmState *zm_g = 0;
#endif /* !ZM_IS_STREAM */

extern unsigned int ZM_DESC[];
typedef void (*zm_post_fn)(void *, unsigned);

ZM_INLINE(zm_in_ram)
static inline int zm_in_ram(uint32_t a)
{
    return (a >= 0xC0000000u && a < 0xC8000000u) || (a >= 0x11800000u && a < 0x11840000u);
}

/* Which known OS is this (ZM_FW_*, 0 = unknown)? Pure reads. */
ZM_INLINE(zm_fw_ok)
static inline uint32_t zm_fw_ok(void)
{
    uint32_t k, i;
    for (k = 0u; k < ZM_NFW; ++k) {
        const uint32_t *sig = &zm_fws[k].sig[0][0];
        for (i = 0u; i < 20u && sig[i]; i += 2u)
            if (!zm_in_ram(sig[i]) || (sig[i] & 3u) || ZM_U32(sig[i]) != sig[i + 1u]) break;
        if (i > 0u && (i == 20u || !sig[i])) return k + 1u;
    }
    return 0u;
}

/* Is this effect's own card or knob page on screen? (zvis_visible without
 * the FXID test.) Only after zm_fw_ok(). */
ZM_INLINE(zm_visible)
static inline int zm_visible(const ZmFw *t, uint32_t own)
{
    uint32_t q, s, i;
    int c;
    if (own >= t->slots) return 0;
    for (i = 0u; i < 3u; ++i)
        if (t->zero[i] && ZM_U32(t->zero[i]) != 0u) return 0;
    if (t->nonzero && ZM_U32(t->nonzero) == 0u) return 0;
    q = t->page ? ZM_U32(t->page) : 0u;
    c = (int)(*(const volatile signed char *)(uintptr_t)t->cursor);
    if (q > 1u || c < 0 || c >= (int)t->slots) return 0;
    s = t->map ? ZM_U32(t->map + t->mstride * q + 4u * (uint32_t)c) : (uint32_t)c;
    return s == own;
}

ZM_INLINE(zm_set_col)
static inline void zm_set_col(const ZmFw *t, uint32_t slot, uint32_t col, uint32_t v)
{
    *(volatile uint32_t *)(uintptr_t)(t->tab + 44u * slot + 4u * col) = v;  /* table first */
    *(volatile uint32_t *)(uintptr_t)(t->rec + 44u * slot + 4u * col) = v;
}

/* Knob raw (value / 100) -> 0..max. */
ZM_INLINE(zm_knob)
static inline uint32_t zm_knob(float raw, uint32_t max)
{
    int k;
    if (!(raw >= 0.0f)) return 0u;      /* NaN, negative */
    k = (int)(raw * 100.0f + 0.5f);
    return (uint32_t)k > max ? max : (uint32_t)k;
}

#if !ZM_IS_STREAM
#if ZM_RADIX
/* All slot values from Floppy's columns (z->fcol). */
ZM_INLINE(zm_decode_all)
static inline void zm_decode_all(ZmState *z)
{
    uint32_t c, i, k, p, n, s, w, r, cur, q, v;
    int rem;
    for (k = 0u; k < ZM_NLIMB; ++k) z->big[k] = 0u;
    for (c = 0u; c < 9u; ++c) {
        p = zm_cols[c].start;
        n = zm_cols[c].n;
        w = z->fcol[c] & ((1u << n) - 1u);
        while (n) {
            s = 16u - (p & 15u);
            if (s > n) s = n;
            z->big[p >> 4] |= (w & ((1u << s) - 1u)) << (p & 15u);
            w >>= s; p += s; n -= s;
        }
    }
    for (i = 0u; i < ZM_NSLOT; ++i) {
        const ZmSlot *d = &zm_slots[i];
        r = d->max + 1u;
        rem = 0;
        for (k = ZM_NLIMB; k-- > 0u; ) {
            cur = ((uint32_t)rem << 16) | z->big[k];          /* < 2^24: exact as float */
            q = (uint32_t)(int)((float)(int)cur * d->inv);
            rem = (int)(cur - q * r);
            if (rem < 0) { q--; rem += (int)r; }
            else if (rem >= (int)r) { q++; rem -= (int)r; }
            z->big[k] = q;
        }
        v = (uint32_t)rem + d->def;
        if (v >= r) v -= r;
        z->vals[i] = (uint16_t)v;
    }
}

/* All slot values into Floppy's columns, writing the changed ones to both rows. */
ZM_INLINE(zm_encode_all)
static inline void zm_encode_all(ZmState *z, const ZmFw *t)
{
    uint32_t c, i, k, p, n, s, w, r, cur, carry, b;
    for (k = 0u; k < ZM_NLIMB; ++k) z->big[k] = 0u;
    for (i = ZM_NSLOT; i-- > 0u; ) {
        const ZmSlot *d = &zm_slots[i];
        r = d->max + 1u;
        carry = z->vals[i] + r - d->def;                      /* the digit */
        if (carry >= r) carry -= r;
        for (k = 0u; k < ZM_NLIMB; ++k) {                     /* big = big * r + digit */
            cur = z->big[k] * r + carry;                      /* < 2^24 */
            z->big[k] = cur & 0xFFFFu;
            carry = cur >> 16;
        }
    }
    for (c = 0u; c < 9u; ++c) {
        p = zm_cols[c].start;
        n = zm_cols[c].n;
        w = 0u; b = 0u;
        while (n) {
            s = 16u - (p & 15u);
            if (s > n) s = n;
            w |= ((z->big[p >> 4] >> (p & 15u)) & ((1u << s) - 1u)) << b;
            b += s; p += s; n -= s;
        }
        if (w != z->fcol[c]) {
            z->fcol[c] = w;
            zm_set_col(t, z->fslot, 2u + c, w);
        }
    }
}
#else
/* Slot value from Floppy's columns (z->fcol). */
ZM_INLINE(zm_decode)
static inline uint32_t zm_decode(const ZmState *z, const ZmSlot *d)
{
    uint32_t raw = 0u, p, v;
    for (p = 0u; p < 3u; ++p) {
        const ZmPart *q = &d->part[p];
        if (q->n)
            raw |= ((z->fcol[q->col] >> q->fsh) & ((1u << q->n) - 1u)) << q->vsh;
    }
    v = (raw + d->def) & ((1u << d->bits) - 1u);
    return v > d->max ? d->def : v;
}

/* Value into Floppy's columns, writing the changed ones to both rows. */
ZM_INLINE(zm_encode)
static inline void zm_encode(ZmState *z, const ZmFw *t, const ZmSlot *d, uint32_t v)
{
    uint32_t raw = (v - d->def) & ((1u << d->bits) - 1u), p;
    for (p = 0u; p < 3u; ++p) {
        const ZmPart *q = &d->part[p];
        if (q->n) {
            uint32_t m = ((1u << q->n) - 1u) << q->fsh;
            uint32_t w = (z->fcol[q->col] & ~m) | (((raw >> q->vsh) << q->fsh) & m);
            z->fcol[q->col] = w;
            zm_set_col(t, z->fslot, 2u + q->col, w);
        }
    }
}
#endif

ZM_INLINE(zm_val)
static inline uint32_t zm_val(const ZmState *z, uint32_t slot)
{
    return z->vals[slot];
}

/* Call at the top of every audio call (after the magic shuttle). */
ZM_INLINE(zm_poll)
static inline void zm_poll(ZmState *z, unsigned int *ctx)
{
    float *params = (float *)(uintptr_t)ctx[1];
    const ZmFw *t = 0;
    uint32_t i, m, f, s, rows, dirty;

    z->slot = (uint32_t)ctx[0];
    z->fw = zm_fw_ok();
    if (z->fw) t = &zm_fws[z->fw - 1u];
    rows = t && z->slot < t->slots;

    if (z->magic != ZM_MAGIC) {
        z->magic = ZM_MAGIC;
        for (i = 0u; i < ZM_NSLOT; ++i) z->vals[i] = zm_slots[i].def;
        for (i = 0u; i < ZM_NKNOB; ++i) z->last[i] = (uint16_t)zm_knob(params[5 + i], zm_kmax[i]);
        for (m = 0u; m < ZM_NMENU; ++m) z->sel[m] = (uint8_t)z->last[zm_menus[m].sel];
        z->fcol[0] = ZM_NOCOL;
        z->posts = 0u; z->postWait = 0u;
    }
    zm_g = z;

    /* Floppy: find it, decode everything when its columns changed */
    z->fslot = ZM_NOSLOT;
    if (rows)
        for (s = 0u; s < t->slots; ++s)
            if ((ZM_U32(t->rec + 44u * s + 4u) & t->idmask) == t->idval) { z->fslot = s; break; }
    if (z->fslot != ZM_NOSLOT) {
        dirty = 0u;
        for (i = 0u; i < 9u; ++i) {
            uint32_t w = ZM_U32(t->tab + 44u * z->fslot + 4u * (2u + i));
            if (w != z->fcol[i]) { z->fcol[i] = w; dirty = 1u; }
        }
        if (dirty) {
#if ZM_RADIX
            zm_decode_all(z);
#else
            for (i = 0u; i < ZM_NSLOT; ++i) z->vals[i] = (uint16_t)zm_decode(z, &zm_slots[i]);
#endif
        }
    } else {
        z->fcol[0] = ZM_NOCOL;
    }

    for (m = 0u; m < ZM_NMENU; ++m) {
        const ZmMenu *mn = &zm_menus[m];
        uint32_t r = zm_knob(params[5 + mn->sel], zm_kmax[mn->sel]);
        uint32_t base = mn->slot0 + r * mn->nfld;
        if (r != z->sel[m]) {
            /* selector moved: the field knobs jump to the record's values */
            z->sel[m] = (uint8_t)r;
            z->last[mn->sel] = (uint16_t)r;
            for (f = 0u; f < mn->nfld; ++f) {
                uint32_t k = mn->sel + 1u + f, v = z->vals[base + f];
                if (rows) {
                    zm_set_col(t, z->slot, 2u + k, v);
                    params[5 + k] = (float)v * 0.01f;
                    z->last[k] = (uint16_t)v;
                } else {
                    z->last[k] = (uint16_t)zm_knob(params[5 + k], zm_kmax[k]);   /* pickup */
                }
            }
            z->posts = ZM_POSTS;
            z->postWait = 0u;
        } else {
            for (f = 0u; f < mn->nfld; ++f) {
                uint32_t k = mn->sel + 1u + f, v = zm_knob(params[5 + k], zm_kmax[k]);
                if (v != z->last[k]) {
                    const ZmSlot *d = &zm_slots[base + f];
                    z->last[k] = (uint16_t)v;
                    if (v > d->max) v = d->max;
                    z->vals[base + f] = (uint16_t)v;
#if ZM_RADIX
                    if (z->fslot != ZM_NOSLOT) zm_encode_all(z, t);
#else
                    if (z->fslot != ZM_NOSLOT) zm_encode(z, t, d, v);
#endif
                }
            }
        }
        if (mn->rename) {
            /* the Value knob's name follows the param (the OS may restore it) */
            volatile unsigned char *name = (volatile unsigned char *)&ZM_DESC[12u * (3u + mn->sel)];
            const char *src = &zm_names[(mn->names + r) * 5u];
            for (i = 0u; i < 8u; ++i) {
                unsigned char c = i < 5u ? (unsigned char)src[i] : 0u;
                if (name[i] != c) name[i] = c;
            }
        }
    }

    if (z->posts) {
        if (z->postWait) z->postWait--;
        else {
            z->posts--;
            z->postWait = ZM_POST_GAP;
            if (t && zm_visible(t, z->slot) && zm_in_ram(t->event) && !(t->event & 3u)) {
                uint32_t ev = ZM_U32(t->event);
                if (zm_in_ram(ev) && !(ev & 3u))
                    ((zm_post_fn)t->sig[0][0])((void *)(uintptr_t)ev, 0x01u);
            }
        }
    }
}

/* Knob text: menu m, field f (ZM_SEL = the selector), raw knob value v.
 * Names come from zm_names (5 chars), else the number (0..150: menu knob
 * maxes are <= 150). "Xy --" on the selector when values can't be saved
 * (known firmware, no Floppy). One shared copy in .text, called by the
 * GetString_Zm* wrappers: the call is PC-relative inside the section (the
 * assembler resolves it, no relocation); inlined it cost ~500 B per knob. */
ZM_EXPAND_PRAGMA(CODE_SECTION(zm_text, ".text"))
ZM_EXPAND_PRAGMA(FUNC_CANNOT_INLINE(zm_text))
static int zm_text(uint32_t m, uint32_t f, uint32_t v, char *s)
{
    const volatile ZmState *z = zm_g;
    const ZmMenu *mn = &zm_menus[m];
    uint32_t names, h = 0u, t = 0u;
    int n = 0;
    if (f == ZM_SEL) {
        if (v > mn->nrec - 1u) v = mn->nrec - 1u;
        names = mn->names;
    } else {
        const ZmSlot *d = &zm_slots[mn->slot0 + (z ? z->sel[m] : 0u) * mn->nfld + f];
        if (v > d->max) v = d->max;
        names = d->names;
    }
    if (names != ZM_NONAMES) {
        const char *p = &zm_names[(names + v) * 5u];
        s[0] = p[0]; s[1] = p[1]; s[2] = p[2]; s[3] = p[3]; s[4] = p[4]; s[5] = 0;
        n = p[4] ? 5 : p[3] ? 4 : p[2] ? 3 : p[1] ? 2 : 1;
    } else {
        if (v >= 100u) { v -= 100u; h = 1u; }
        if (v >= 80u) { v -= 80u; t += 8u; }
        if (v >= 40u) { v -= 40u; t += 4u; }
        if (v >= 20u) { v -= 20u; t += 2u; }
        if (v >= 10u) { v -= 10u; t += 1u; }
        if (h) s[n++] = '1';
        if (h || t) s[n++] = (char)('0' + t);
        s[n++] = (char)('0' + v);
        s[n] = 0;
    }
    if (f == ZM_SEL && z && z->fw && z->fslot == ZM_NOSLOT) {
        if (n < 2) s[1] = ' ';
        s[2] = ' '; s[3] = '-'; s[4] = '-'; s[5] = 0;
        n = 5;
    }
    return n;
}

#else /* ZM_IS_STREAM */

#define ZM_S_BITS 106u                  /* 13,13,13,8,8,8,8,27,8 (G1on; MS assumed) */
#define ZM_S_PARTS 4u                   /* Floppies: part 1 + 3 marked copies */
#define ZM_S_MARK  11u                  /* marker of part q (0-based, >= 1) = 11 + q */
static const uint8_t zm_s_wid[9] = { 13u, 13u, 13u, 8u, 8u, 8u, 8u, 27u, 8u };

typedef struct {
    uint32_t magic;
    uint32_t slot, fw, fslot;           /* own slot, ZM_FW_*, part 1's slot */
    uint32_t pslot[ZM_S_PARTS];         /* slot of part q, ZM_NOSLOT = missing */
    uint32_t nparts, gap;               /* parts 1..nparts present; a part after a missing one */
    uint32_t pmap;                      /* pslot[q] & 15 in nibble q (changes = redraw) */
    uint32_t posts, postWait;
    uint32_t fcol[ZM_S_PARTS][9];       /* each part's columns as last read / written */
    uint32_t fwx;                       /* ~fw: fw is checked again when it doesn't match */
} ZmStream;

/* Own and Floppy rows (any known OS). */
ZM_INLINE(zm_s_fw)
static inline const ZmFw *zm_s_fw(const ZmStream *z)
{
    return z->fw ? &zm_fws[z->fw - 1u] : 0;
}

/* Part of the Floppy in slot s: 0 unmarked, 1..ZM_S_PARTS - 1 marked,
 * ZM_S_PARTS no Floppy there (or a marker of a part this build has no room
 * for: then unmarked too, see zm_s_poll). */
ZM_INLINE(zm_s_mark)
static inline uint32_t zm_s_mark(const ZmFw *t, uint32_t s)
{
    uint32_t m;
    if ((ZM_U32(t->rec + 44u * s + 4u) & t->idmask) != t->idval) return ZM_S_PARTS;
    m = ZM_U32(t->tab + 44u * s + 8u) & 15u;
    return m > ZM_S_MARK && m - ZM_S_MARK < ZM_S_PARTS ? m - ZM_S_MARK : 0u;
}

/* Call at the top of every audio call (after the magic shuttle). Finds the
 * Floppies (parts, see above) and reads their columns; returns 1 when they
 * differ from what was last read or written (Floppy added or removed, patch
 * loaded, first call), so the effect should decode them (zm_s_get). Without
 * part 1 fslot = ZM_NOSLOT. */
ZM_INLINE(zm_s_poll)
static inline uint32_t zm_s_poll(ZmStream *z, unsigned int *ctx)
{
    const ZmFw *t;
    uint32_t s, i, q, m, w, pass, mk, dirty = 0u;
    z->slot = (uint32_t)ctx[0];
    /* the OS can't change while running: its fingerprint is checked again
     * only after a reset, or when fw doesn't match its copy (the state was
     * overwritten while lent out) */
    if (z->magic != ZM_MAGIC || z->fwx != ~z->fw || z->fw > ZM_NFW) {
        z->fw = zm_fw_ok();
        z->fwx = ~z->fw;
    }
    if (z->magic != ZM_MAGIC) {
        z->magic = ZM_MAGIC;
        for (q = 0u; q < ZM_S_PARTS; ++q) z->fcol[q][0] = ZM_NOCOL;
        z->posts = 0u; z->postWait = 0u;
    }
    t = zm_s_fw(z);                     /* any known OS (MS pedals untested) */
    for (q = 0u; q < ZM_S_PARTS; ++q) z->pslot[q] = ZM_NOSLOT;
    if (t && z->slot < t->slots) {
        /* marked copies first (a repeated marker: the first one), then the
         * unmarked ones: pass 1 the first non-empty is part 1, pass 2 the
         * first of the rest if still none; the others (and repeated
         * markers) get a free marker */
        for (s = 0u, mk = 0u; s < t->slots; ++s) {     /* marks read once: nibble s (<= 8 slots) */
            q = zm_s_mark(t, s);
            mk |= q << (4u * s);
            if (q - 1u < ZM_S_PARTS - 1u && z->pslot[q] == ZM_NOSLOT) z->pslot[q] = s;
        }
        for (pass = 1u; pass <= 2u; ++pass)
            for (s = 0u; s < t->slots; ++s) {
                q = (mk >> (4u * s)) & 15u;
                if (q == ZM_S_PARTS || (q && z->pslot[q] == s) || z->pslot[0] == s) continue;
                if (q == 0u && z->pslot[0] == ZM_NOSLOT) {
                    w = pass - 1u;      /* pass 2: any */
                    for (i = 0u; i < 9u; ++i)
                        w |= ZM_U32(t->tab + 44u * s + 4u * (2u + i)) & ((1u << zm_s_wid[i]) - 1u);
                    if (w) z->pslot[0] = s;
                } else if (pass == 2u) {    /* an extra copy, or a repeated marker */
                    for (m = 1u; m < ZM_S_PARTS && z->pslot[m] != ZM_NOSLOT; ++m) {}
                    if (m < ZM_S_PARTS) {   /* a new copy: marker, the rest zeros */
                        z->pslot[m] = s;
                        for (i = 0u; i < 9u; ++i) zm_set_col(t, s, 2u + i, i ? 0u : ZM_S_MARK + m);
                    }
                }
            }
    }
    z->fslot = z->pslot[0];
    z->nparts = 0u;
    z->gap = 0u;
    z->pmap = 0u;
    for (q = 0u; q < ZM_S_PARTS; ++q) {
        s = z->pslot[q];
        z->pmap |= (s & 15u) << (4u * q);
        if (s == ZM_NOSLOT) {
            if (z->fcol[q][0] != ZM_NOCOL) { z->fcol[q][0] = ZM_NOCOL; dirty = 1u; }
            continue;
        }
        if (z->nparts == q) z->nparts = q + 1u;
        else z->gap = 1u;
        for (i = 0u; i < 9u; ++i) {
            w = ZM_U32(t->tab + 44u * s + 4u * (2u + i)) & ((1u << zm_s_wid[i]) - 1u);
            if (w != z->fcol[q][i]) { z->fcol[q][i] = w; dirty = 1u; }
        }
    }
    return dirty;
}

/* All parts' 106 bits -> w[4 q .. 4 q + 3] (part q), bit 0 first (column 1's
 * bit 0); a missing part reads as zeros. Markers are left in. */
ZM_INLINE(zm_s_get)
static inline void zm_s_get(const ZmStream *z, uint32_t *w)
{
    uint32_t q, c, p, v, sh;
    for (q = 0u; q < ZM_S_PARTS; ++q, w += 4) {
        w[0] = 0u; w[1] = 0u; w[2] = 0u; w[3] = 0u;
        if (z->pslot[q] == ZM_NOSLOT) continue;
        for (c = 0u, p = 0u; c < 9u; ++c) {
            v = z->fcol[q][c];
            sh = p & 31u;
            w[p >> 5] |= v << sh;
            if (sh + zm_s_wid[c] > 32u) w[(p >> 5) + 1u] |= v >> (32u - sh);
            p += zm_s_wid[c];
        }
    }
}

/* w[4 q .. 4 q + 3] -> part q's columns for the parts 1..nparts, writing
 * the changed ones to both rows; extra parts get their marker in bits
 * 0..3. Returns 0 without part 1 (nothing saved). */
ZM_INLINE(zm_s_put)
static inline uint32_t zm_s_put(ZmStream *z, const uint32_t *w)
{
    const ZmFw *t = zm_s_fw(z);
    uint32_t q, c, p, v, sh;
    if (z->nparts == 0u || !t) return 0u;
    for (q = 0u; q < z->nparts; ++q, w += 4) {
        for (c = 0u, p = 0u; c < 9u; ++c) {
            sh = p & 31u;
            v = w[p >> 5] >> sh;
            if (sh + zm_s_wid[c] > 32u) v |= w[(p >> 5) + 1u] << (32u - sh);
            v &= (1u << zm_s_wid[c]) - 1u;
            if (c == 0u && q) v = (v & ~15u) | (ZM_S_MARK + q);
            if (v != z->fcol[q][c]) {
                z->fcol[q][c] = v;
                zm_set_col(t, z->pslot[q], 2u + c, v);
            }
            p += zm_s_wid[c];
        }
    }
    return 1u;
}

/* Own knob k (0-based) jumps to v (its patch columns and params[]) and the
 * page is redrawn. Returns 0 on an unknown OS (knob not moved). */
ZM_INLINE(zm_s_jump)
static inline uint32_t zm_s_jump(ZmStream *z, unsigned int *ctx, uint32_t k, uint32_t v)
{
    const ZmFw *t = zm_s_fw(z);
    float *params = (float *)(uintptr_t)ctx[1];
    if (!t || z->slot >= t->slots) return 0u;
    zm_set_col(t, z->slot, 2u + k, v);
    params[5u + k] = (float)v * 0.01f;
    z->posts = ZM_POSTS;
    z->postWait = 0u;
    return 1u;
}

/* Redraw posts after jumps; call once per audio call. */
ZM_INLINE(zm_s_post)
static inline void zm_s_post(ZmStream *z)
{
    const ZmFw *t = zm_s_fw(z);
    if (!z->posts) return;
    if (z->postWait) { z->postWait--; return; }
    z->posts--;
    z->postWait = ZM_POST_GAP;
    if (t && zm_visible(t, z->slot) && zm_in_ram(t->event) && !(t->event & 3u)) {
        uint32_t ev = ZM_U32(t->event);
        if (zm_in_ram(ev) && !(ev & 3u))
            ((zm_post_fn)t->sig[0][0])((void *)(uintptr_t)ev, 0x01u);
    }
}

#endif /* ZM_IS_STREAM */

#endif /* ZMENU_H */
