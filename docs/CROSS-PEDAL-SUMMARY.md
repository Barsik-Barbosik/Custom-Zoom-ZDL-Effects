# One ZDL for both pedal types (original MS and G1on family)

Both families run the same effect interface. A ZDL that stays inside that interface runs on both unchanged: DrumBox was built for the MS-70CDR and runs as-is on the G1Xon. Where the two differ, follow the stricter pedal.

## Always required (no firmware check)

| Rule | Why |
|---|---|
| `--mem_model:data=far`, `.fardata` ~0, no `.bss`, no libm / `double` / integer `/` `%` | Same firmware loader on both families |
| `static const` tables need `.rela.dyn` entries (our linker adds them) | Without them, table addresses point to the wrong place |
| `_init` calls `ctx[34](params, zeros, n)` before the knob handlers | Required on the G1Xon; every stock `_init` makes this call on both families |
| Declared DSP cost (descriptor +0x28) honest and within the G1on budget of **210** | MS-70CDR is ~228, MS-60B 225 |
| Allocated size (`.text` + `.const` + `.fardata`) **≤ ~29,968 B** | The G1Xon freezes on select above this (30,216 B froze); the MS limit is unknown |
| Effect name ≤ 8 characters, fxid ≤ 1023, gid matches the SONAME prefix | Patch format on the MS pedals |
| Expect fewer instances per patch: G1on 5, MS-70CDR / MS-50G 6, MS-60B 4 | Slots per patch |
| Large state goes in `ctx[3]`. Audio data stored there must be harmless if played as float, other data at the arena's end. Checksum anything you need to trust. | The next effect gets the memory as it is; patch switches lend it out |
| Hide the firmware knobs with your own blank 1x1 knob bitmap (`knob_bitmap`), not by changing the knob records while running | Uses only the standard picture format, works on both |

## Works on both without a firmware check

If the effect doesn't need to force redraws, leave out the whole firmware-specific part:

* the firmware check (code-word compares)
* the address tables
* the redraw posting and the "on screen" logic

That saves a few hundred bytes, more if you would otherwise carry two firmware tables. The file then runs on every ZDL pedal and firmware version, not only the ones it has addresses for.

Without the check you still get:

* Audio processing, knobs, knob ranges and display laws
* `getstr` knob text (up to 5 characters on the G1on)
* A static picture
* A picture that changes: point imageInfo word 5 at your own image (double-buffered). The new image shows up only when the firmware redraws on its own. That happens on page changes and knob events on both pedals, and on knob turns on the G1on. This is enough for a picture that changes when a knob moves.

Never keep the address calls and drop only the check. That is the one combination that can crash an unknown pedal.

## Needs a firmware check

You need a check only when the effect reads or calls fixed firmware addresses:

| Feature | Needs |
|---|---|
| **Forcing a redraw** (a picture that moves with the audio, ~30 fps) | `Event_post` + the screen task's event object |
| Knowing whether your effect is on screen (to post redraws only when it is) | Selected-effect byte, slot records; MS: page variables, G1on: count `getstr` calls |
| Reading slot state (On/Off, which slot holds you) | Slot record address |
| Using the footswitch as a button (G1on) | On/Off words at 0x1181B160 / 0x1181B370 |

How the check works:
1. At start-up, read a few code words spread over the code you depend on, and compare them with the known values for each supported firmware (MS-70CDR 2.10, G1on family 1.21).
2. If one matches, use that firmware's address table. If none matches, do nothing beyond the standard interface (a static picture, plain audio).
3. Check again before every firmware call. On a wrong guess the effect calls into random code.

Each firmware needs its own address table. One file covers only the firmware versions it has tables for.

## Addresses

| What | MS-70CDR 2.10 | G1on family 1.21 |
|---|---|---|
| `Event_post(evt, mask)` | 0xC00CBF40 | 0xC00B05C0 |
| Screen task event object | `*(0xC00EF9D8)` | `*(0xC00DEBCC)` |
| Redraw bit | 0x01 | 0x01 (never post 0x40: it loops the boot animation) |
| Data page B14 | 0xC00EF748 | 0xC00DEA00 |
| Selected effect (byte, 0-based) | 0xC009C064 | 0xC0078CB4 (equals `ctx[0]` when your effect is selected) |
| Slot records, 44-byte rows | 0xC009BF58 | 0x1181B160 (live patch: word +0 On/Off, word +4 = gid << 24 \| fxid) |
| `state[31]` slot table (col 0 = On/Off copy) | 0xC009C1A0 | 0x1181B370 |
| Saved patch copy (EDITED = differs from live) | — | 0x1181B268 |
| Logo page vs knob page | 0xC009D398 and others | no variable; count `getstr` calls |
| Selected knob (1-based) | — | 0xC0078CAC |
| `Event_pend` / `Semaphore_pend` | 0xC00BE680 / 0xC00C7EE0 | 0xC009F700 / 0xC00AB540 |
| LCD send | 0xC00B57C8 | 0xC0093CE4 |
| Fingerprint words | `Event_post` starts `27773577 85778677` (LM_DINO checks 18 more words) | `0xC00B05C0..D0` = `27773577 85778677 B64631F7 02C53A2B 01C50628`; `0xC009B278` = `0200736D` |

The G1on column is hardware-tested except the saved patch copy, `Event_pend` / `Semaphore_pend` and LCD send (static analysis). The MS-70CDR column comes from LM_DINO's table and matches the 2.10 updater, but was not tested on hardware here.
