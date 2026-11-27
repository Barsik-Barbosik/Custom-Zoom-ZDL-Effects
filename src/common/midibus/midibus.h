/* midibus: MIDI messages between ZDL effects in one chain (G1on family:
 * discovery tested on the B1Xon 1.30; MS-70CDR 2.10, MS-50G 3.10, MS-60B
 * 2.10: same descriptor spacing in their firmware, untested).
 *
 * Every chain slot has a memory area (ctx[3] points at its descriptor:
 * word 0 base, word 1 end, word 2 length). The descriptors lie 12 B apart
 * in slot order, so slot s's descriptor = own ctx[3] + 12 * (s - own slot)
 * on every known firmware. The areas belong to the slots (fixed addresses)
 * and are not cleared on a patch switch, replace or move.
 *
 * A sender keeps a port block in the last MB_PORT bytes of its own area
 * (end rounded down to 8): magic, info (version, flags, own slot),
 * heartbeat (+1 every call), write counter (bytes ever written) and a ring
 * of raw MIDI bytes (byte i at ring[i % MB_RING]). The host effect must
 * not use those bytes.
 *
 * A receiver looks at every other slot's port each call (only reads). A
 * port counts when magic, version, out flag and slot match and its
 * heartbeat changed MB_LIVE calls (stale ports from removed effects stay
 * in memory with a frozen heartbeat); it is dropped after MB_DEAD calls
 * without a change. A new port is read from its current counter on (no
 * replay). Several senders are merged in slot order; own port skipped.
 *
 * Rate: a sender writes at most MB_RING bytes per call (the budget), so a
 * receiver before it in the chain (reading one call later) loses nothing.
 * More: drops, counted by the receiver.
 *
 * Messages: channel messages 0x80..0xEF (running status accepted),
 * real-time 0xF8..0xFF (anywhere, always passed). System common and SysEx
 * (0xF0..0xF7) are skipped by the receiver and refused by mb_out_msg.
 *
 * Usage, every audio call (before the host's footswitch check: effects
 * switched off are still called):
 *   sender:   p = mb_out_tick(ctx, MB_F_OUT);
 *             if (p) mb_out_msg(p, 0x90 | ch, note, vel);   (0 = no room)
 *   receiver: mb_in_poll(&in, ctx);
 *             while (mb_in_next(&in, &st, &d1, &d2)) { ... }
 * MbIn lives in the host's ctx[3] state, zeroed at setup; set chmask
 * (bit c = channel c + 1; MB_ALLCH = all).
 *
 * Rules (ZDL): force-inlined, no stack arrays, no divide, no globals. */
#ifndef MIDIBUS_H
#define MIDIBUS_H
#include <stdint.h>

#define MB_DO_PRAGMA(x) _Pragma(#x)
#define MB_EXPAND_PRAGMA(x) MB_DO_PRAGMA(x)
#define MB_INLINE(f) MB_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(f))

#define MB_MAGIC   0x4944494Du          /* "MIDI" */
#define MB_VERSION 1u
#define MB_PORT    128u                 /* port block: the area's last 128 B */
#define MB_RING    64u                  /* ring bytes (power of 2) */
#define MB_NS      6u                   /* slots scanned (the most on a known pedal) */
#define MB_LIVE    8u                   /* heartbeat changes before a port counts */
#define MB_DEAD    16u                  /* calls without a change before it is dropped */
#define MB_F_OUT   1u                   /* info flags */
#define MB_F_IN    2u
#define MB_ALLCH   0xFFFFu
#define MB_MINLEN  0x1000u              /* plausible area: 4 KB .. 2 MB in DDR */
#define MB_MAXLEN  0x200000u

typedef struct {
    uint32_t magic;
    uint32_t info;                      /* version | flags << 8 | slot << 16 */
    uint32_t hb;                        /* +1 every call */
    uint32_t count;                     /* bytes ever written (wraps) */
    uint8_t ring[MB_RING];
    uint32_t used;                      /* sender only: bytes written this call */
    uint32_t pad[11];
} MbPort;                               /* MB_PORT bytes */

typedef struct {
    uint32_t port[MB_NS];               /* port address while it counts, else 0 */
    uint32_t hb[MB_NS];                 /* heartbeat at the last poll */
    uint32_t pos[MB_NS];                /* next byte to read */
    uint32_t live[MB_NS];               /* heartbeat changes seen (up to MB_LIVE) */
    uint32_t dead[MB_NS];               /* calls without a change */
    uint32_t rs[MB_NS];                 /* running status, 0 = none */
    uint32_t n[MB_NS];                  /* data bytes collected (0 / 1) */
    uint32_t d0[MB_NS];                 /* first data byte */
    uint32_t cur;                       /* slot mb_in_next reads */
    uint32_t from;                      /* slot of the last message */
    uint32_t chmask;                    /* bit c = channel c + 1 passes */
    uint32_t drops;                     /* bytes lost to ring overflow */
    uint32_t skipped;                   /* channel messages filtered out */
} MbIn;

/* The port block of the area whose descriptor is at d, or 0. Pure reads. */
MB_INLINE(mb_port_at)
static inline uint32_t mb_port_at(uint32_t d)
{
    uint32_t b, e;
    if (d < 0xC0000000u || d >= 0xC7FFFFF0u || (d & 3u)) return 0u;
    b = *(const volatile uint32_t *)(uintptr_t)d;
    e = *(const volatile uint32_t *)(uintptr_t)(d + 4u) & ~7u;
    if (b < 0xC0000000u || e > 0xC8000000u || e <= b) return 0u;
    if (e - b < MB_MINLEN || e - b > MB_MAXLEN) return 0u;
    return e - MB_PORT;
}

/* ---- sender ---- */

/* Once per audio call, before any mb_out_*: sets the port up when it is
 * not this effect's (fresh area, leftovers), heartbeat, opens this call's
 * budget. Returns the port, or 0 (no usable area). */
MB_INLINE(mb_out_tick)
static inline volatile MbPort *mb_out_tick(const unsigned int *ctx, uint32_t flags)
{
    uint32_t a = mb_port_at((uint32_t)ctx[3]);
    uint32_t info = MB_VERSION | flags << 8 | ((uint32_t)ctx[0] & 255u) << 16;
    volatile MbPort *p;
    if (!a) return 0;
    p = (volatile MbPort *)(uintptr_t)a;
    if (p->magic != MB_MAGIC || p->info != info) {
        p->magic = 0u;
        p->info = info;
        p->count = 0u;
        p->hb = 0u;
        p->magic = MB_MAGIC;            /* last */
    }
    p->hb = p->hb + 1u;
    p->used = 0u;
    return p;
}

/* Bytes left in this call's budget. */
MB_INLINE(mb_out_room)
static inline uint32_t mb_out_room(volatile MbPort *p)
{
    return MB_RING - p->used;
}

/* One raw byte (status or data, e.g. for running status). 1 = written,
 * 0 = no room this call. */
MB_INLINE(mb_out_raw)
static inline uint32_t mb_out_raw(volatile MbPort *p, uint32_t b)
{
    uint32_t c = p->count;
    if (p->used >= MB_RING) return 0u;
    p->ring[c & (MB_RING - 1u)] = (uint8_t)b;
    p->count = c + 1u;                  /* after the byte */
    p->used = p->used + 1u;
    return 1u;
}

/* One whole message: channel message (2 or 3 bytes by status) or real-time
 * (1 byte). 1 = written, 0 = no room this call or not supported (0xF0..
 * 0xF7). Data bytes are masked to 7 bits. */
MB_INLINE(mb_out_msg)
static inline uint32_t mb_out_msg(volatile MbPort *p, uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t c = p->count, len;
    if (st >= 0xF8u) len = 1u;
    else if (st >= 0xF0u || st < 0x80u) return 0u;
    else len = (st & 0xE0u) == 0xC0u ? 2u : 3u;
    if (p->used + len > MB_RING) return 0u;
    p->ring[c & (MB_RING - 1u)] = (uint8_t)st;
    if (len > 1u) p->ring[(c + 1u) & (MB_RING - 1u)] = (uint8_t)(d1 & 127u);
    if (len > 2u) p->ring[(c + 2u) & (MB_RING - 1u)] = (uint8_t)(d2 & 127u);
    p->count = c + len;                 /* after the bytes */
    p->used = p->used + len;
    return 1u;
}

/* ---- receiver ---- */

/* Once per audio call: finds the live ports of the other slots, checks for
 * overflow, restarts reading at slot 0. */
MB_INLINE(mb_in_poll)
static inline void mb_in_poll(volatile MbIn *in, const unsigned int *ctx)
{
    uint32_t own = (uint32_t)ctx[0], d3 = (uint32_t)ctx[3], s, a, h, c, info;
    volatile MbPort *p;
#pragma UNROLL(1)
    for (s = 0u; s < MB_NS; ++s) {
        a = s == own ? 0u : mb_port_at(d3 + 12u * s - 12u * own);
        p = (volatile MbPort *)(uintptr_t)a;
        info = a ? p->info : 0u;
        if (!a || p->magic != MB_MAGIC || (info & 255u) != MB_VERSION ||
            !(info >> 8 & MB_F_OUT) || (info >> 16 & 255u) != s) {
            in->port[s] = 0u;           /* no port: start over when one appears */
            in->live[s] = 0u;
            in->dead[s] = 0u;
            continue;
        }
        h = p->hb;
        if (h != in->hb[s]) {
            in->hb[s] = h;
            in->dead[s] = 0u;
            if (in->live[s] < MB_LIVE && ++in->live[s] == MB_LIVE) {
                in->pos[s] = p->count;  /* counts from now on */
                in->rs[s] = 0u;
                in->n[s] = 0u;
            }
        } else if (++in->dead[s] >= MB_DEAD) {
            in->live[s] = 0u;
            in->dead[s] = 0u;
        }
        if (in->live[s] < MB_LIVE) { in->port[s] = 0u; continue; }
        in->port[s] = a;
        c = p->count;
        if (c - in->pos[s] > MB_RING) { /* overwritten before read */
            in->drops += c - in->pos[s] - MB_RING;
            in->pos[s] = c - MB_RING;
            in->rs[s] = 0u;             /* wait for the next status byte */
            in->n[s] = 0u;
        }
    }
    in->cur = 0u;
}

/* The next message of this call (all ports, slot order): 1 + *st / *d1 /
 * *d2 (real-time: d1 = d2 = 0; 2-byte messages: d2 = 0), in->from = the
 * sender's slot; 0 = none left. */
MB_INLINE(mb_in_next)
static inline uint32_t mb_in_next(volatile MbIn *in, uint32_t *st, uint32_t *d1, uint32_t *d2)
{
    uint32_t s, b, r;
    volatile MbPort *p;
#pragma UNROLL(1)
    while ((s = in->cur) < MB_NS) {
        p = (volatile MbPort *)(uintptr_t)in->port[s];
        if (!p || in->pos[s] == p->count) { in->cur = s + 1u; continue; }
        b = p->ring[in->pos[s] & (MB_RING - 1u)];
        in->pos[s] = in->pos[s] + 1u;
        if (b >= 0xF8u) {               /* real-time: anywhere, state kept */
            in->from = s;
            *st = b; *d1 = 0u; *d2 = 0u;
            return 1u;
        }
        if (b >= 0xF0u) { in->rs[s] = 0u; in->n[s] = 0u; continue; }  /* system / SysEx: skipped */
        if (b & 0x80u) { in->rs[s] = b; in->n[s] = 0u; continue; }
        r = in->rs[s];
        if (!r) continue;               /* data without a status */
        if ((r & 0xE0u) != 0xC0u && !in->n[s]) { in->d0[s] = b; in->n[s] = 1u; continue; }
        in->n[s] = 0u;
        if (!(in->chmask >> (r & 15u) & 1u)) { in->skipped++; continue; }
        in->from = s;
        *st = r;
        if ((r & 0xE0u) == 0xC0u) { *d1 = b; *d2 = 0u; }
        else { *d1 = in->d0[s]; *d2 = b; }
        return 1u;
    }
    return 0u;
}

#endif /* MIDIBUS_H */
