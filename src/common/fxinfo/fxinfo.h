/* fxinfo: an effect tells the other effects in the chain what it offers,
 * for example a sound source its sounds' names. Independent of midibus
 * (an effect may publish without MIDI and use MIDI without publishing).
 * Layout and rules (the contract for other implementations): README.md in
 * this folder.
 *
 * Every chain slot has a memory area (ctx[3] points at its descriptor:
 * word 0 base, word 1 end, word 2 length; slot s's descriptor = own ctx[3]
 * + 12 * (s - own slot)). The info block lies right below the area's last
 * FI_TOP bytes (end rounded down to 8; those bytes are midibus' port
 * place, used or not): a 16-B header (FiHdr) at end - FI_TOP - FI_HDR,
 * the record (its length in the header) directly below the header. The
 * header and the record together take at most FI_AREA bytes; a publishing
 * host keeps them (and the FI_TOP bytes) free.
 *
 * Record: items one after another, each a word FI_ITEM(type, count,
 * width) followed by count entries of width bytes (padded to 4 bytes at
 * the item's end); type FI_END ends it early. Readers skip unknown types.
 *
 * Writer: fi_publish(ctx, rec, n) at least every FI_PUB calls (rec = the
 * record words, static const). It writes the block when it is not this
 * record (fresh area, moved, overwritten), else only counts the heartbeat.
 * Host duty: not while its slot is being replaced (its record no longer
 * its own): the area then belongs to the next effect.
 *
 * Reader: fi_at_slot() checks a slot's header (magic, version, slot
 * field, length, sum); count a block only when its heartbeat moved since
 * your previous look (looks at least FI_LOOK calls apart): blocks of
 * removed effects stay in memory with a frozen heartbeat. fi_item() finds
 * an item. Copy what you need into your own state (the other area may
 * change any time).
 *
 * Rules (ZDL): force-inlined, no stack arrays, no divide, no globals. */
#ifndef FXINFO_H
#define FXINFO_H

#include <stdint.h>

#define FI_DO_PRAGMA(x) _Pragma(#x)
#define FI_EXPAND_PRAGMA(x) FI_DO_PRAGMA(x)
#define FI_INLINE(f) FI_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(f))

#define FI_MAGIC   0x4F464E49u          /* "INFO" in memory */
#define FI_VERSION 1u
#define FI_TOP     128u                 /* bytes above the header (midibus' port place) */
#define FI_HDR     16u                  /* header bytes */
#define FI_AREA    256u                 /* header + record, at most */
#define FI_MAXREC  (FI_AREA - FI_HDR)   /* record bytes, at most */
#define FI_PUB     128u                 /* writer: calls between publishes, at most */
#define FI_LOOK    256u                 /* reader: calls between looks, at least */
#define FI_MINLEN  0x1000u              /* plausible area: 4 KB .. 2 MB in DDR */
#define FI_MAXLEN  0x200000u

/* Items: a word type | count << 8 | width << 16 (bits 24..31: 0), then
 * count entries of width bytes. */
#define FI_ITEM(type, count, width) ((uint32_t)(type) | (uint32_t)(count) << 8 | (uint32_t)(width) << 16)
#define FI_END      0u                  /* end of the record */
#define FI_SOUNDS   1u                  /* sounds in program order (sound value / program 0, 1, ..), width 4:
                                         * byte 0 kind, bytes 1..3 the name (ASCII 0x20..0x7E, NUL padded) */
#define FI_CHANNELS 2u                  /* count 1, width 4: bit c = MIDI channel c + 1 is played */
/* type 3: planned (a drum kit's note names) */
#define FI_DATA     4u                  /* storage knobs other effects may write (Floppy, SndBank; MelodyBox reads it),
                                         * width 4: byte 0 knob 1..9, byte 1 the patch field's bits,
                                         * byte 2 low bits the publisher keeps (others leave them),
                                         * byte 3 flags (0) */
#define FI_DATA4(knob, bits, keep) ((uint32_t)(knob) | (uint32_t)(bits) << 8 | (uint32_t)(keep) << 16)
#define FI_K_MELODIC 0u                 /* sound kinds */
#define FI_K_DRUMS   1u                 /* a drum kit (its notes' names: a later item type) */
/* A 4-byte entry (kind + 3 name characters) as a word. */
#define FI_ENTRY4(kind, a, b, c) ((uint32_t)(kind) | (uint32_t)(a) << 8 | (uint32_t)(b) << 16 | (uint32_t)(c) << 24)

typedef struct {
    uint32_t magic;                     /* FI_MAGIC, written last */
    uint32_t meta;                      /* version | slot << 8 | record bytes << 16 */
    uint32_t sum;                       /* meta + the record's words (wrapping) */
    uint32_t hb;                        /* heartbeat: +1 at every publish */
} FiHdr;

/* The header address of the area whose descriptor is at d, or 0. Pure
 * reads. */
FI_INLINE(fi_hdr_at)
static inline uint32_t fi_hdr_at(uint32_t d)
{
    uint32_t b, e;
    if (d < 0xC0000000u || d >= 0xC7FFFFF0u || (d & 3u)) return 0u;
    b = *(const volatile uint32_t *)(uintptr_t)d;
    e = *(const volatile uint32_t *)(uintptr_t)(d + 4u) & ~7u;
    if (b < 0xC0000000u || e > 0xC8000000u || e <= b) return 0u;
    if (e - b < FI_MINLEN || e - b > FI_MAXLEN) return 0u;
    return e - FI_TOP - FI_HDR;
}

/* ---- writer ---- */

/* Publish the record rec (n words) in the own area: rewritten when the
 * block is not this record, the heartbeat counted. Returns 1 when
 * published, 0 when there is no usable area. */
FI_INLINE(fi_publish)
static inline uint32_t fi_publish(const unsigned int *ctx, const uint32_t *rec, uint32_t n)
{
    uint32_t a = fi_hdr_at((uint32_t)ctx[3]), meta, sum, same, i;
    volatile FiHdr *h;
    volatile uint32_t *r;
    if (!a || n > FI_MAXREC / 4u) return 0u;
    h = (volatile FiHdr *)(uintptr_t)a;
    r = (volatile uint32_t *)(uintptr_t)(a - 4u * n);
    meta = FI_VERSION | ((uint32_t)ctx[0] & 255u) << 8 | (4u * n) << 16;
    sum = meta;
    same = h->magic == FI_MAGIC && h->meta == meta;
#pragma UNROLL(1)
    for (i = 0u; i < n; i++) {
        sum += rec[i];
        if (r[i] != rec[i]) same = 0u;
    }
    if (!same || h->sum != sum) {
        h->magic = 0u;
#pragma UNROLL(1)
        for (i = 0u; i < n; i++) r[i] = rec[i];
        h->meta = meta;
        h->sum = sum;
        h->magic = FI_MAGIC;            /* last */
    }
    h->hb = h->hb + 1u;
    return 1u;
}

/* ---- reader ---- */

/* The header address of slot s's info block (d3 = own ctx[3], own = own
 * slot), or 0 when there is none: magic, version, slot field, length and
 * sum checked. The heartbeat is the caller's (FiHdr hb). */
FI_INLINE(fi_at_slot)
static inline uint32_t fi_at_slot(uint32_t d3, uint32_t own, uint32_t s)
{
    uint32_t a = fi_hdr_at(d3 + 12u * s - 12u * own), m, n, sum, i;
    const volatile FiHdr *h;
    const volatile uint32_t *r;
    if (!a) return 0u;
    h = (const volatile FiHdr *)(uintptr_t)a;
    if (h->magic != FI_MAGIC) return 0u;
    m = h->meta;
    n = m >> 16;
    if ((m & 255u) != FI_VERSION || (m >> 8 & 255u) != s || n > FI_MAXREC || (n & 3u)) return 0u;
    r = (const volatile uint32_t *)(uintptr_t)(a - n);
    sum = m;
#pragma UNROLL(1)
    for (i = 0u; i < n >> 2; i++) sum += r[i];
    return sum == h->sum ? a : 0u;
}

/* The address of the first item of the given type in the record of the
 * header at a (from fi_at_slot), or 0. */
FI_INLINE(fi_item)
static inline uint32_t fi_item(uint32_t a, uint32_t type)
{
    uint32_t p = a - (((const volatile FiHdr *)(uintptr_t)a)->meta >> 16), w, q;
#pragma UNROLL(1)
    while (p < a) {
        w = *(const volatile uint32_t *)(uintptr_t)p;
        if ((w & 255u) == FI_END) return 0u;
        q = p + 4u + (((w >> 8 & 255u) * (w >> 16 & 255u) + 3u) & ~3u);
        if (q > a) return 0u;           /* runs past the record: broken */
        if ((w & 255u) == type) return p;
        p = q;
    }
    return 0u;
}

#endif
