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
 * heartbeat (+1 every call), write counter (bytes ever written), a seal
 * = counter ^ epoch (written after the counter; next to it: one cache
 * line for the receiver's every-call read) and a ring of raw MIDI bytes
 * (byte i at ring[i % MB_RING]). The epoch changes whenever a sender
 * sets the port up (new effect in the slot, other flags), so a receiver
 * tells a new stream from the old one; a port whose seal does not match
 * (overwritten by another effect) is dropped. The host effect must not
 * use those bytes.
 *
 * A receiver scans the other slots' ports every MB_SCAN calls (only
 * reads): a port counts once its magic, version, out flag and slot match
 * and its heartbeat moved at two scans in a row (stale ports from removed
 * effects stay in memory with a frozen heartbeat); it is dropped at the
 * first scan where the heartbeat stood still, or at once when its magic
 * or seal is wrong (new epoch: a new sender, read from its counter once
 * it qualifies again). Between scans only the live ports' write counter
 * and seal are read (an overwritten port fails the seal). A new port is read from its current counter
 * on (no replay). Several senders are merged in slot order; own port
 * skipped. Every port that stops counting sets its bit in in.gone: the
 * host releases the notes it holds from that slot (mb_in_gone).
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
 *   sender:   if (mb_out_tick(&out, ctx, MB_F_OUT))
 *                 mb_out_msg(&out, 0x90 | ch, note, vel);   (0 = no room)
 *   receiver: mb_in_poll(&in, ctx);
 *             g = mb_in_gone(&in);   (bit s: release slot s's notes)
 *             while (mb_in_next(&in, &st, &d1, &d2)) { ... }
 * MbOut / MbIn live in the host's ctx[3] state, zeroed at setup (best
 * 64-B aligned: the state is in DDR, every touched cache line costs); set
 * in.chmask (bit c = channel c + 1; MB_ALLCH = all). Pass plain MbOut /
 * MbIn pointers held in locals: with a cast at the call (e.g. dropping
 * the host state's volatile) TI CGT 8.3 left the functions out of line
 * in .text (called from .audio) despite FUNC_ALWAYS_INLINE.
 * Host duty: when the host knows its slot is being replaced (its record
 * no longer its own, e.g. zmenu's tables), stop calling mb_out_tick: the
 * area then belongs to the next effect.
 *
 * Rules (ZDL): force-inlined, no stack arrays, no divide, no globals. */
#ifndef MIDIBUS_H
#define MIDIBUS_H
#include <stdint.h>

#define MB_DO_PRAGMA(x) _Pragma(#x)
#define MB_EXPAND_PRAGMA(x) MB_DO_PRAGMA(x)
#define MB_INLINE(f) MB_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(f))

#define MB_MAGIC   0x4944494Du          /* "MIDI" */
#define MB_VERSION 2u                   /* 2: seal word (1: probes only) */
#define MB_PORT    128u                 /* port block: the area's last 128 B */
#define MB_RING    64u                  /* ring bytes (power of 2) */
#define MB_NS      6u                   /* slots scanned (the most on a known pedal) */
#define MB_SCAN    16u                  /* calls between scans (receiver) / port checks (sender) */
#define MB_F_OUT   1u                   /* info flags */
#define MB_F_IN    2u
#define MB_ALLCH   0xFFFFu
#define MB_EPOCH   0x9E3779B9u          /* epoch step at a set-up */
#define MB_MINLEN  0x1000u              /* plausible area: 4 KB .. 2 MB in DDR */
#define MB_MAXLEN  0x200000u

typedef struct {
    uint32_t magic;
    uint32_t info;                      /* version | flags << 8 | slot << 16 */
    uint32_t hb;                        /* +1 every call */
    uint32_t count;                     /* bytes ever written (wraps) */
    uint32_t seal;                      /* count ^ epoch, after count */
    uint8_t ring[MB_RING];
    uint32_t pad[11];                   /* reserved */
} MbPort;                               /* MB_PORT bytes */

typedef struct {
    uint32_t port;                      /* own port block, 0 = none */
    uint32_t key;                       /* ctx[3] at the last check (a move changes it) */
    uint32_t scan;                      /* calls to the next check */
    uint32_t hb, count, used;           /* the port's heartbeat / counter; bytes this call */
    uint32_t ep;                        /* the port's epoch */
} MbOut;

typedef struct {                        /* per slot, 32 B */
    uint32_t port;                      /* port address seen at the scans */
    uint32_t hb;                        /* heartbeat at the last scan */
    uint32_t ep;                        /* epoch seen at the scans */
    uint32_t pos;                       /* next byte to read */
    uint32_t end;                       /* write counter at this call's poll */
    uint32_t rs;                        /* running status, 0 = none */
    uint32_t n;                         /* data bytes collected (0 / 1) */
    uint32_t d0;                        /* first data byte */
} MbInSlot;

typedef struct {
    uint32_t scan;                      /* calls to the next scan */
    uint32_t live;                      /* bit s: the port counts */
    uint32_t todo;                      /* bit s: bytes to read this call */
    uint32_t gone;                      /* bit s: the port stopped counting (mb_in_gone) */
    uint32_t cur;                       /* slot mb_in_next reads */
    uint32_t chmask;                    /* bit c = channel c + 1 passes */
    uint32_t from;                      /* slot of the last message */
    uint32_t seen;                      /* bit s: heartbeat moved at the last scan */
    uint32_t drops;                     /* bytes lost to ring overflow */
    uint32_t skipped;                   /* channel messages filtered out */
    uint32_t pad[6];
    MbInSlot sl[MB_NS];
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

/* Once per audio call, before any mb_out_*: every MB_SCAN calls (and after
 * a move) finds the own port and sets it up when it is not this effect's
 * (fresh area, leftovers); every call the heartbeat and this call's
 * budget. Returns the port, or 0 (no usable area). */
MB_INLINE(mb_out_tick)
static inline volatile MbPort *mb_out_tick(MbOut *o, const unsigned int *ctx, uint32_t flags)
{
    uint32_t d3 = (uint32_t)ctx[3];
    volatile MbPort *p;
    if (d3 != o->key || !o->scan) {
        uint32_t a = mb_port_at(d3);
        uint32_t info = MB_VERSION | flags << 8 | ((uint32_t)ctx[0] & 255u) << 16;
        o->key = d3;
        o->scan = MB_SCAN;
        o->port = a;
        if (a) {
            p = (volatile MbPort *)(uintptr_t)a;
            o->ep = p->count ^ p->seal; /* the port's epoch (or junk) */
            if (p->magic != MB_MAGIC || p->info != info) {
                o->ep += MB_EPOCH;      /* a new stream: never the old epoch */
                p->magic = 0u;
                p->info = info;
                p->count = 0u;
                p->seal = o->ep;
                p->hb = 0u;
                p->magic = MB_MAGIC;    /* last */
            }
            o->hb = p->hb;
            o->count = p->count;
        }
    }
    o->scan--;
    o->used = 0u;
    if (!o->port) return 0;
    p = (volatile MbPort *)(uintptr_t)o->port;
    o->hb++;
    p->hb = o->hb;
    return p;
}

/* Bytes left in this call's budget. */
MB_INLINE(mb_out_room)
static inline uint32_t mb_out_room(const MbOut *o)
{
    return o->port ? MB_RING - o->used : 0u;
}

/* One raw byte (status or data, e.g. for running status). 1 = written,
 * 0 = no room this call. */
MB_INLINE(mb_out_raw)
static inline uint32_t mb_out_raw(MbOut *o, uint32_t b)
{
    volatile MbPort *p = (volatile MbPort *)(uintptr_t)o->port;
    uint32_t c = o->count;
    if (!p || o->used >= MB_RING) return 0u;
    p->ring[c & (MB_RING - 1u)] = (uint8_t)b;
    o->count = c + 1u;
    p->count = c + 1u;                  /* after the byte */
    p->seal = (c + 1u) ^ o->ep;
    o->used++;
    return 1u;
}

/* One whole message: channel message (2 or 3 bytes by status) or real-time
 * (1 byte). 1 = written, 0 = no room this call or not supported (0xF0..
 * 0xF7). Data bytes are masked to 7 bits. */
MB_INLINE(mb_out_msg)
static inline uint32_t mb_out_msg(MbOut *o, uint32_t st, uint32_t d1, uint32_t d2)
{
    volatile MbPort *p = (volatile MbPort *)(uintptr_t)o->port;
    uint32_t c = o->count, len;
    if (st >= 0xF8u) len = 1u;
    else if (st >= 0xF0u || st < 0x80u) return 0u;
    else len = (st & 0xE0u) == 0xC0u ? 2u : 3u;
    if (!p || o->used + len > MB_RING) return 0u;
    p->ring[c & (MB_RING - 1u)] = (uint8_t)st;
    if (len > 1u) p->ring[(c + 1u) & (MB_RING - 1u)] = (uint8_t)(d1 & 127u);
    if (len > 2u) p->ring[(c + 2u) & (MB_RING - 1u)] = (uint8_t)(d2 & 127u);
    o->count = c + len;
    p->count = c + len;                 /* after the bytes */
    p->seal = (c + len) ^ o->ep;
    o->used += len;
    return 1u;
}

/* ---- receiver ---- */

/* Once per audio call: every MB_SCAN calls looks for ports in the other
 * slots (liveness); every call reads the live ports' write counters,
 * checks for overflow and marks the ports with bytes to read. */
MB_INLINE(mb_in_poll)
static inline void mb_in_poll(MbIn *in, const unsigned int *ctx)
{
    uint32_t s, a, c, m, bit, todo = 0u;
    volatile MbPort *p;
    if (in->scan) in->scan--;
    else {
        uint32_t own = (uint32_t)ctx[0], d3 = (uint32_t)ctx[3], info, h, e, ok;
        in->scan = MB_SCAN - 1u;
#pragma UNROLL(1)
        for (s = 0u; s < MB_NS; ++s) {
            bit = 1u << s;
            a = s == own ? 0u : mb_port_at(d3 + 12u * s - 12u * own);
            p = (volatile MbPort *)(uintptr_t)a;
            h = 0u;
            c = 0u;
            e = 0u;
            ok = 0u;
            if (a && p->magic == MB_MAGIC) {    /* the rest only behind a magic */
                info = p->info;
                h = p->hb;
                c = p->count;
                e = c ^ p->seal;
                ok = (info & 255u) == MB_VERSION && (info >> 8 & MB_F_OUT) &&
                     (info >> 16 & 255u) == s && h != in->sl[s].hb &&
                     a == in->sl[s].port && e == in->sl[s].ep;
            }
            if (!ok) {
                if (in->live & bit) in->gone |= bit;
                in->seen &= ~bit;       /* none, stale, stopped, new here or new epoch */
                in->live &= ~bit;
                in->sl[s].hb = h;
                in->sl[s].port = a;
                in->sl[s].ep = e;
                continue;
            }
            in->sl[s].hb = h;
            if ((in->seen & bit) && !(in->live & bit)) {
                in->live |= bit;        /* counts from now on */
                in->sl[s].pos = c;
                in->sl[s].rs = 0u;
                in->sl[s].n = 0u;
            }
            in->seen |= bit;
        }
    }
    m = in->live;
#pragma UNROLL(1)
    for (s = 0u; m; ++s, m >>= 1) {
        if (!(m & 1u)) continue;
        p = (volatile MbPort *)(uintptr_t)in->sl[s].port;
        c = p->count;
        if ((c ^ p->seal) != in->sl[s].ep) {
            in->live &= ~(1u << s);     /* overwritten or a new stream: gone at once */
            in->seen &= ~(1u << s);
            in->gone |= 1u << s;
            continue;
        }
        if (c - in->sl[s].pos > MB_RING) { /* overwritten before read */
            in->drops += c - in->sl[s].pos - MB_RING;
            in->sl[s].pos = c - MB_RING;
            in->sl[s].rs = 0u;             /* wait for the next status byte */
            in->sl[s].n = 0u;
        }
        in->sl[s].end = c;
        if (c != in->sl[s].pos) todo |= 1u << s;
    }
    in->todo = todo;
    in->cur = 0u;
}

/* Slots whose port stopped counting since the last call (removed,
 * stopped, replaced, a new stream): release the notes held from them.
 * Clears the bits. */
MB_INLINE(mb_in_gone)
static inline uint32_t mb_in_gone(MbIn *in)
{
    uint32_t g = in->gone;
    in->gone = 0u;
    return g;
}

/* The next message of this call (all ports, slot order): 1 + *st / *d1 /
 * *d2 (real-time: d1 = d2 = 0; 2-byte messages: d2 = 0), in->from = the
 * sender's slot; 0 = none left. */
MB_INLINE(mb_in_next)
static inline uint32_t mb_in_next(MbIn *in, uint32_t *st, uint32_t *d1, uint32_t *d2)
{
    uint32_t s, b, r, pos, end;
    const volatile uint8_t *ring;
#pragma UNROLL(1)
    while (in->todo) {
        s = in->cur;
        if (!(in->todo >> s & 1u)) { in->cur = s + 1u; continue; }
        ring = ((const volatile MbPort *)(uintptr_t)in->sl[s].port)->ring;
        pos = in->sl[s].pos;
        end = in->sl[s].end;
        r = in->sl[s].rs;
#pragma UNROLL(1)
        while (pos != end) {
            b = ring[pos & (MB_RING - 1u)];
            pos++;
            if (b >= 0xF8u) {           /* real-time: anywhere, state kept */
                in->sl[s].pos = pos;
                in->from = s;
                *st = b; *d1 = 0u; *d2 = 0u;
                return 1u;
            }
            if (b >= 0xF0u) { r = 0u; in->sl[s].rs = 0u; in->sl[s].n = 0u; continue; }  /* system / SysEx: skipped */
            if (b & 0x80u) { r = b; in->sl[s].rs = b; in->sl[s].n = 0u; continue; }
            if (!r) continue;           /* data without a status */
            if ((r & 0xE0u) != 0xC0u && !in->sl[s].n) { in->sl[s].d0 = b; in->sl[s].n = 1u; continue; }
            in->sl[s].n = 0u;
            if (!(in->chmask >> (r & 15u) & 1u)) { in->skipped++; continue; }
            in->sl[s].pos = pos;
            in->from = s;
            *st = r;
            if ((r & 0xE0u) == 0xC0u) { *d1 = b; *d2 = 0u; }
            else { *d1 = in->sl[s].d0; *d2 = b; }
            return 1u;
        }
        in->sl[s].pos = pos;
        in->todo &= ~(1u << s);
        in->cur = s + 1u;
    }
    return 0u;
}

#endif /* MIDIBUS_H */
