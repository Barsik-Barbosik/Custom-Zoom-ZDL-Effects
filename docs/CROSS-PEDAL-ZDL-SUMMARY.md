# One ZDL for both pedal types (original MS and G1on family)

Both families run the same effect interface. A ZDL that stays inside that interface runs on both unchanged: DrumBox was built for the MS-70CDR and runs as-is on the G1Xon. Where the two differ, follow the stricter pedal.

G1on-family findings were tested on a B1Xon pedal with both G1Xon firmware 1.21 and B1Xon firmware 1.30. The G1on, G1Xon, B1on and B1Xon share one OS image (G1on / G1Xon 1.21 = B1on / B1Xon 1.30), so the addresses below hold for all four. themanro's Skull, ported with the rules below, runs on both firmwares with a live picture. So do his other live-picture effects (Ring, Scope, Spectrum, Loop) with the table-driven firmware check described below (2026-10-29).

## Always required (no firmware check)

| Rule | Why |
|---|---|
| `--mem_model:data=far`, `.fardata` ~0, no `.bss`, no libm / `double` / integer `/` `%` | Same firmware loader on both families |
| `static const` tables need `.rela.dyn` entries (our linker adds them) | Without them, table addresses point to the wrong place |
| `_init` calls `ctx[34](params, zeros, n)` before the knob handlers | Every stock `_init` makes this call on both families. DrumBox (9 knobs) froze or had dead knobs on G1Xon 1.21 without it; Skull (4 knobs) works without it on B1Xon 1.30. When it is needed is not known yet, so make the call |
| Declared DSP cost (descriptor +0x28) honest and within the G1on budget of **210** | MS-70CDR is ~228, MS-60B 225 |
| Allocated size (`.text` + `.const` + `.fardata`) **≤ ~29,968 B** | The G1Xon freezes on select above this (30,216 B froze); the MS limit is unknown. If the effect never divides floats, leave out the 640-byte `__c6xabi_divf` routine the linkers always add: that alone brought Skull with both firmwares back under the limit |
| Effect name ≤ 8 characters, fxid ≤ 1023, gid matches the SONAME prefix | Patch format on the MS pedals |
| Expect fewer instances per patch: G1on 5, MS-70CDR / MS-50G 6, MS-60B 4 | Slots per patch |
| Large state goes in `ctx[3]`. Audio data stored there must be harmless if played as float, other data at the arena's end. Checksum anything you need to trust. | The next effect gets the memory as it is; patch switches lend it out |
| Hide the firmware knobs with your own blank 1x1 knob bitmap (`knob_bitmap`) | Uses only the standard picture format, works on both. Changing the knob records while running (themanro's Skull) also works on the G1on family |

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
1. At start-up, read a few code words spread over the code you depend on, and compare them with the known values for each supported firmware (MS-70CDR 2.10, G1on family: G1on / G1Xon 1.21, B1on / B1Xon 1.30).
2. If one matches, use that firmware's address table. If none matches, do nothing beyond the standard interface (a static picture, plain audio).
3. Check again before every firmware call. On a wrong guess the effect calls into random code.

Each firmware needs its own address table. One file covers only the firmware versions it has tables for.

Keeping the check small (themanro's `zvis.h`, table version, tested on B1Xon 1.30 and G1Xon 1.21):

* Put each firmware in a `.const` table entry: fingerprint words as (address, value) pairs, then its addresses (~120 B per firmware). One shared loop checks the fingerprints and one shared function tests "on screen", so another firmware costs a table entry, not code. Two firmwares written as inline compares cost ~1.3 KB of code; as a table, ~400 B plus the entries.
* Make `Event_post` the first fingerprint address and call that address, so the function called is always code that was just checked.
* At start-up copy the matching entry into your state in `ctx[3]`, so the table is addressed once (each use of a `.const` table costs an address pair and two `.rela.dyn` entries). Patch switches lend that memory out, so check the copy's fingerprint again every callback before using any of it, and skip any fingerprint address outside RAM or not word-aligned without reading it.
* Ask which firmware, not only whether one is known. Anything that is mapped on one firmware only must test for that firmware: themanro's Loop used "firmware known" to mean "MS-70CDR" for its extra RAM regions, and would have used those MS-70CDR addresses on the G1on.

## Addresses

| What | MS-70CDR 2.10 | G1on family (G1on / G1Xon 1.21, B1on / B1Xon 1.30) |
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
| Fingerprint words | `Event_post` starts `27773577 85778677` (LM_DINO checks 18 more words) | `0xC00B05C0..D0` = `27773577 85778677 B64631F7 02C53A2B 01C50628`; `0xC009B278` = `0200736D`, `0xC009B280` = `10089013`, `0xC008B318` = `02C65A28`, `0xC008B320` = `02104CA0` |

The G1on column is hardware-tested (G1Xon 1.21; `Event_post`, event object, redraw bit, selected effect and fingerprint also on B1Xon 1.30) except the saved patch copy, `Event_pend` / `Semaphore_pend` and LCD send (static analysis). The MS-70CDR column comes from LM_DINO's table and matches the 2.10 updater, but was not tested on hardware here.
