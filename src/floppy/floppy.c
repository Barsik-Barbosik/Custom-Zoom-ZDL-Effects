/*
 * Floppy -- storage companion for zmenu.h menus.
 *
 * Does nothing to the sound. Its 9 knobs exist only so the patch saves
 * their fields (G1on widths 13,13,13,8,8,8,8,27,8 bits); zmenu.h finds this
 * effect's slot by its record word +4 (gid 7 << 24 | fxid 530) and packs the
 * menu values into its knob columns. Build: python -B build.py
 * --out P (1 flash block: no knob texts, the firmware shows its own
 * numbers). With FL_KNOB_TEXT (build_floppy(knob_text=True)) the knob pages
 * show the raw column values (~350 B more, 2 blocks).
 *
 * fxinfo (src/common/fxinfo): Floppy publishes a DATA item (its 9 knobs,
 * all bits free) so storage users can find it without its id. About one
 * call in 64 (a TSCL window: Floppy has no state) the whole block is
 * rewritten (the record is constant, its sum known at compile time but for
 * the slot number), only while the own slot's record still holds Floppy
 * (fxinfo writer rule 2). The record rows per OS are picked by one
 * fingerprint word (Event_post's 4th word, zmenu.h's zm_fwt val[0]); an
 * unknown OS or a wrong match means no publish, never a write outside the
 * own area. Size: ~15 B left in the one flash block.
 *
 * Rules: getstr without calls or division (its loops become SPLOOPs, which
 * stay interruptible, as in DrumBox and stock effects).
 */

#include <stdint.h>
#include <c6x.h>
#include "fxinfo.h"

#define FL_AUDIO_FUNC Fx_SFX_Floppy

#define FL_DO_PRAGMA(x) _Pragma(#x)
#define FL_EXPAND_PRAGMA(x) FL_DO_PRAGMA(x)
FL_EXPAND_PRAGMA(CODE_SECTION(FL_AUDIO_FUNC, ".audio"))

#define FL_ID ((7u << 24) | 530u)       /* own slot record word +4 (gid 7, fxid 530) */
#define FL_U32(a) (*(const volatile uint32_t *)(uintptr_t)(a))

/* The DATA item: knobs 1-9 at the G1on field widths, nothing kept. */
#define FL_E0 FI_ITEM(FI_DATA, 9u, 4u)
#define FL_E1 FI_DATA4(1u, 13u, 0u)
#define FL_E2 FI_DATA4(2u, 13u, 0u)
#define FL_E3 FI_DATA4(3u, 13u, 0u)
#define FL_E4 FI_DATA4(4u, 8u, 0u)
#define FL_E5 FI_DATA4(5u, 8u, 0u)
#define FL_E6 FI_DATA4(6u, 8u, 0u)
#define FL_E7 FI_DATA4(7u, 8u, 0u)
#define FL_E8 FI_DATA4(8u, 27u, 0u)
#define FL_E9 FI_DATA4(9u, 8u, 0u)
#define FL_N 10u                        /* record words */
#define FL_META0 (FI_VERSION | (4u * FL_N) << 16)     /* meta without the slot */
#define FL_SUM0 (FL_META0 + FL_E0 + FL_E1 + FL_E2 + FL_E3 + FL_E4 + FL_E5 + FL_E6 + FL_E7 + FL_E8 + FL_E9)
static const uint32_t fl_info[FL_N] = { FL_E0, FL_E1, FL_E2, FL_E3, FL_E4, FL_E5, FL_E6, FL_E7, FL_E8, FL_E9 };

void FL_AUDIO_FUNC(unsigned int *ctx)
{
    unsigned int *magicSrc = (unsigned int *)(uintptr_t)ctx[12];
    unsigned int *magicDst = (unsigned int *)(uintptr_t)(*(unsigned int *)(uintptr_t)ctx[11]);
    uint32_t a, o, rec, i;
    volatile uint32_t *h, *r;
    *magicDst = *magicSrc;
    /* ~32.6k cycles per call: a 2^15-cycle window every 2^21 cycles */
    if ((TSCL & 0x1FFFFFu) >= 0x8000u) return;
    o = 44u * (uint32_t)ctx[0] + 4u;
    if (FL_U32(0xC00B05CCu) == 0x02C53A2Bu) rec = 0x1181B160u;          /* G1on family */
    else if (FL_U32(0xC00CBF4Cu) == 0x02E7442Bu
          || FL_U32(0xC00CC68Cu) == 0x02E9C02Bu) rec = 0xC009BF58u;     /* MS-70CDR, MS-50G (untested) */
    else if (FL_U32(0xC00BC50Cu) == 0x02EA982Bu) rec = 0xC008C6A0u;     /* MS-60B (untested) */
    else return;
    if ((uint32_t)ctx[0] > 5u || FL_U32(rec + o) != FL_ID) return;    /* slot being replaced */
    a = fi_hdr_at((uint32_t)ctx[3]);
    if (!a) return;
    h = (volatile uint32_t *)(uintptr_t)a;                  /* FiHdr: magic, meta, sum, hb */
    r = (volatile uint32_t *)(uintptr_t)(a - 4u * FL_N);
    h[0] = 0u;
#pragma UNROLL(1)
    for (i = 0u; i < FL_N; i++) r[i] = fl_info[i];
    o = ((uint32_t)ctx[0] & 255u) << 8;
    h[1] = FL_META0 | o;
    h[2] = FL_SUM0 + o;
    h[0] = FI_MAGIC;                    /* last */
    h[3] = h[3] + 1u;
}

#ifdef FL_KNOB_TEXT
FL_EXPAND_PRAGMA(CODE_SECTION(GetString_FlNum, ".text"))

/* The raw column value in decimal, 0..65535; above 65535 the bit length
 * "b17".."b32" instead (knob 8's 27-bit field). x / 10 = (x * 52429) >> 19,
 * exact for x <= 65535. Two short loops instead of an unrolled compare
 * chain (512 -> 224 B). */
int GetString_FlNum(unsigned int v, char *s)
{
    unsigned int t;
    int i = 0, n = 0;
    if (v > 65535u) {
        s[0] = 'b';
        s++;
        n = 1;
        v = 32u - (unsigned int)_lmbd(1u, v);
    }
    t = v;
#pragma UNROLL(1)
    do { i++; t = (t * 52429u) >> 19; } while (t);
    n += i;
    s[i] = 0;
#pragma UNROLL(1)
    do { t = (v * 52429u) >> 19; s[--i] = (char)('0' + v - t * 10u); v = t; } while (v);
    return n;
}
#endif
