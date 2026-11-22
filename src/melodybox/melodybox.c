/*
 * Melody Box (MeloBox): a note sequencer / player, in development.
 *
 * Knobs 1-3 show three neighbouring items of a list: "" (empty), steps
 * 01..64, "END". A step's name is its number plus its note: "01___" (no
 * note), "01__C" .. "01__B" or "01_A#" (always 5 characters). Knob 3
 * scrolls: its value is the scroll offset, so turning it right moves the
 * three knobs one item on, left one back. Knob 2 edits the note of the
 * step it shows (0 = none, 1..12 = C .. B); on a scroll it jumps to the new
 * step's note. Knob 1 does nothing yet. Knob texts: knob 1 "NOTE", knob 2 its own name, knob 3 "SCROLL".
 *
 * The notes live in the state only (ctx[3]): not saved with the patch, lost
 * on a patch switch.
 *
 * The names are the descriptor entries of knobs 1-3, rewritten by the audio
 * callback every call (the OS may restore them). When a name changed, the
 * screen is redrawn (3 Event_post calls, ZM_POST_GAP calls apart, only while
 * this effect is on screen and the firmware is known: zmenu.h's table).
 * Knob 2's jump writes its patch columns, so it also needs a known firmware
 * (else the knob keeps its position and a turn sets the note from there).
 * Audio passes through untouched.
 */

#include <stdint.h>

#define ZM_STREAM      1
#define ZM_DESC        _Fx_SFX_MeloBox_Desc
#define ZM_MAGIC       0x4D425831u      /* "MBX1" */
#define ZM_FLOPPY_FXID 522u             /* unused (no Floppy access) */
#define ZM_S_RANGE     1                /* own only the Floppies up to the next Floppy user */
#include "zmenu.h"

#pragma CODE_SECTION(Fx_SFX_MeloBox, ".audio")

#define MB_MAGIC  0x4D425831u           /* "MBX1"; new value on any state layout change */
#define MB_NKNOB  3u                    /* knobs showing items */
#define MB_EDIT   1u                    /* knob 2 (0-based): the note of its step */
#define MB_SCROLL 2u                    /* knob 3 (0-based): the scroll offset */
#define MB_NSTEP  64u                   /* steps 1..64; item 0 = "", item 65 = "END" */
#define MB_MAXOFF 63u                   /* knob 3 max: last view = 63 64 END */
#define MB_MAXNOTE 12u                  /* knob 2 max: 0 = none, 1..12 = C .. B */

/* State at the ctx[3] arena's end, magic at both ends. */
typedef struct {
    uint32_t magic0;
    uint32_t posts, postWait;           /* redraws left, calls until the next */
    uint32_t off, edit;                 /* knob 3 / knob 2 values as last seen */
    uint8_t  note[MB_NSTEP + 1u];       /* note per step (index 0 unused) */
    uint32_t magic1;
} MbState;

/* Note letters (index = note, 0 = none); sharps: notes 2, 4, 7, 9, 11. */
static const char mb_letter[MB_MAXNOTE + 1u] = ".CCDDEFFGGAAB";
#define MB_SHARPS 0xA94u                /* bit n set: note n is a sharp */

void Fx_SFX_MeloBox(unsigned int *ctx)
{
    unsigned int *magicSrc = (unsigned int *)(uintptr_t)ctx[12];
    unsigned int *magicDst = (unsigned int *)(uintptr_t)(*(unsigned int *)(uintptr_t)ctx[11]);
    float *params = (float *)(uintptr_t)ctx[1];
    volatile unsigned int *arena = (volatile unsigned int *)(uintptr_t)ctx[3];
    volatile MbState *st;
    uint32_t off, ed, k, n, tens, i, note, changed = 0u, fw;

    *magicDst = *magicSrc;              /* audio passes through untouched */

    if (!arena) return;
    {
        uintptr_t base = (uintptr_t)arena[0];
        uintptr_t end = (uintptr_t)arena[1] & ~(uintptr_t)3u;
        if (base == 0u || end <= base + sizeof(MbState)) return;
        st = (volatile MbState *)(end - sizeof(MbState));
    }
    /* knob 3's value = the scroll offset; knob 2's = the note of step off + 1 */
    off = zm_knob(params[5 + MB_SCROLL], MB_MAXOFF);
    ed = zm_knob(params[5 + MB_EDIT], MB_MAXNOTE);

    if (st->magic0 != MB_MAGIC || st->magic1 != MB_MAGIC) {
        st->magic0 = MB_MAGIC;
        st->posts = 0u;
        st->postWait = 0u;
#pragma UNROLL(1)
        for (i = 0u; i <= MB_NSTEP; i++) st->note[i] = 0u;
        st->note[off + 1u] = (uint8_t)ed;   /* knob 2's patch value: its step's note */
        st->off = off;
        st->edit = ed;
        st->magic1 = MB_MAGIC;
    }

    if (off != st->off) {
        /* scrolled: knob 2 jumps to its new step's note (own patch columns +
         * params, as zmenu's jump); unknown firmware: it stays (pickup) */
        st->off = off;
        note = st->note[off + 1u];
        fw = zm_fw_ok();
        if (fw && (uint32_t)ctx[0] < zm_fws[fw - 1u].slots) {
            zm_set_col(&zm_fws[fw - 1u], (uint32_t)ctx[0], 2u + MB_EDIT, note);
            params[5 + MB_EDIT] = (float)note * 0.01f;
            ed = note;
        }
        st->edit = ed;
    } else if (ed != st->edit) {
        /* knob 2 turned: the note of its step */
        st->edit = ed;
        st->note[off + 1u] = (uint8_t)ed;
    }

    /* knob k's name: item off + k; "" for item 0, "END" for MB_NSTEP + 1, else
     * the step's 2 digits, then "___" (no note), "__" + a natural note or
     * "_" + a sharp one */
#pragma UNROLL(1)
    for (k = 0u; k < MB_NKNOB; k++) {
        volatile unsigned char *nm = (volatile unsigned char *)&ZM_DESC[12u * (2u + k)];
        n = off + k;
        tens = (n * 205u) >> 11;        /* n / 10, exact for n < 1029 */
        note = n - 1u < MB_NSTEP ? st->note[n] : 0u;
        if (note > MB_MAXNOTE) note = 0u;
#pragma UNROLL(1)
        for (i = 0u; i < 8u; i++) {
            unsigned char v = n == 0u ? 0u :
                              n > MB_NSTEP ? (i == 0u ? 'E' : i == 1u ? 'N' : i == 2u ? 'D' : 0u) :
                              i == 0u ? (unsigned char)('0' + tens) :
                              i == 1u ? (unsigned char)('0' + n - 10u * tens) :
                              i >= 5u ? 0u :
                              i == 2u || note == 0u ? '_' :
                              (MB_SHARPS >> note & 1u) ?
                                  (i == 3u ? (unsigned char)mb_letter[note] : '#') :
                                  (i == 3u ? '_' : (unsigned char)mb_letter[note]);
            if (nm[i] != v) {
                nm[i] = v;
                changed = 1u;
            }
        }
    }
    if (changed) {
        st->posts = ZM_POSTS;
        st->postWait = 0u;
    }

    /* redraw after a rename (only on a known OS, own card on screen) */
    if (st->posts) {
        if (st->postWait) st->postWait--;
        else {
            st->posts--;
            st->postWait = ZM_POST_GAP;
            fw = zm_fw_ok();
            if (fw) {
                const ZmFw *t = &zm_fws[fw - 1u];
                if (zm_visible(t, (uint32_t)ctx[0]) && zm_in_ram(t->event) && !(t->event & 3u)) {
                    uint32_t ev = ZM_U32(t->event);
                    if (zm_in_ram(ev) && !(ev & 3u))
                        ((zm_post_fn)t->post)((void *)(uintptr_t)ev, 0x01u);
                }
            }
        }
    }
}

/* ---- knob texts (UI thread) ---- */

/* Knob 1: always "NOTE". */
#pragma CODE_SECTION(GetString_MbNote, ".text")
int GetString_MbNote(unsigned int v, char *s)
{
    (void)v;
    s[0] = 'N';
    s[1] = 'O';
    s[2] = 'T';
    s[3] = 'E';
    s[4] = 0;
    return 4;
}

/* Knob 2: its own name (descriptor entry 3, as the audio callback wrote it). */
#pragma CODE_SECTION(GetString_MbItem, ".text")
int GetString_MbItem(unsigned int v, char *s)
{
    const volatile unsigned char *nm = (const volatile unsigned char *)&ZM_DESC[12u * 3u];
    int n = 0;
    (void)v;
#pragma UNROLL(1)
    while (n < 7 && nm[n] >= ' ' && nm[n] <= '~') {
        s[n] = (char)nm[n];
        n++;
    }
    s[n] = 0;
    return n;
}

/* Knob 3: always "SCROLL". */
#pragma CODE_SECTION(GetString_MbScroll, ".text")
int GetString_MbScroll(unsigned int v, char *s)
{
    (void)v;
    s[0] = 'S';
    s[1] = 'C';
    s[2] = 'R';
    s[3] = 'O';
    s[4] = 'L';
    s[5] = 'L';
    s[6] = 0;
    return 6;
}
