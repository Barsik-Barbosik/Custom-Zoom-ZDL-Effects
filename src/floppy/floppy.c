/*
 * Floppy -- storage companion for zmenu.h menus.
 *
 * Does nothing to the sound. Its 9 knobs exist only so the patch saves
 * their fields (G1on widths 13,13,13,8,8,8,8,27,8 bits); zmenu.h finds this
 * effect's slot by its record word +4 (gid 7 << 24 | fxid 522) and packs the
 * menu values into its knob columns. Build: python -B build.py
 * --out P (1 flash block: no knob texts, the firmware shows its own
 * numbers). With FL_KNOB_TEXT (build_floppy(knob_text=True)) the knob pages
 * show the raw column values (~350 B more, 2 blocks).
 *
 * Rules: getstr without calls or division (its loops become SPLOOPs, which
 * stay interruptible, as in DrumBox and stock effects).
 */

#include <stdint.h>

#define FL_AUDIO_FUNC Fx_SFX_Floppy

#define FL_DO_PRAGMA(x) _Pragma(#x)
#define FL_EXPAND_PRAGMA(x) FL_DO_PRAGMA(x)
FL_EXPAND_PRAGMA(CODE_SECTION(FL_AUDIO_FUNC, ".audio"))

void FL_AUDIO_FUNC(unsigned int *ctx)
{
    unsigned int *magicSrc = (unsigned int *)(uintptr_t)ctx[12];
    unsigned int *magicDst = (unsigned int *)(uintptr_t)(*(unsigned int *)(uintptr_t)ctx[11]);
    *magicDst = *magicSrc;
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
