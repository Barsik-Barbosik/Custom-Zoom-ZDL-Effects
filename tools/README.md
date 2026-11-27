# G1on-family USB SysEx tools

Two Python scripts for the G1on family (G1on, G1Xon, B1on, B1Xon) over USB
MIDI SysEx: one to manage effect files and drive the pedal, one to keep
the pedal answering between runs. They were written while developing the
effects in this repository.

> **Experimental.** These tools write the pedal's flash without a
> firmware update and without Zoom Effect Manager 2. They were tested on a
> B1Xon only (with both G1Xon 1.21 and B1Xon 1.30 firmware). They don't
> back up the pedal, and a bad delete or a full pedal can leave you with a
> missing effect. To install effects normally, use
> [Zoom Effect Manager 2](https://zoomeffectmanager.com/en/) (see the
> [main README](../README.md#install)).

| Script | What |
|---|---|
| `g1on_sysex.py` | List, upload and delete effect files; disk usage; raw flash read; read the current patch or a saved one; set a knob live; switch patches; read the current patch number and the global settings |
| `g1on_keepalive.py` | Sends an identity request every N seconds so the pedal doesn't stop answering SysEx between tool runs |

## Setup

- Python 3.8+ with `mido` and `python-rtmidi` (`pip install -r
  ../requirements.txt`).
- The pedal connected over USB. Windows shows its MIDI ports as
  "ZOOM 1 Series" (`--port` picks another name).
- MIDI ports are exclusive on Windows: close Zoom Effect Manager 2 and
  any other MIDI program first.

## `g1on_sysex.py`

```
python -B tools/g1on_sysex.py list                        # files on the pedal, with sizes
python -B tools/g1on_sysex.py usage                       # flash size and bytes available
python -B tools/g1on_sysex.py upload path/NAME.ZDL --yes  # write a file
python -B tools/g1on_sysex.py delete NAME.ZDL --yes       # delete a file
python -B tools/g1on_sysex.py patch [-o patch.syx]        # read the current patch
python -B tools/g1on_sysex.py stored N [-o stored.syx]    # read saved patch N without switching
python -B tools/g1on_sysex.py knob SLOT PARAM VALUE       # set a knob of the current patch live
python -B tools/g1on_sysex.py program N                   # switch to patch N (0..99)
python -B tools/g1on_sysex.py current                     # number of the current patch
python -B tools/g1on_sysex.py settings                    # global settings (tempo, tuner, ...)
python -B tools/g1on_sysex.py patchinfo                   # number of patches and patch size
python -B tools/g1on_sysex.py status                      # status value
python -B tools/g1on_sysex.py tuner on|off                # UNTESTED: tuner on / off
python -B tools/g1on_sysex.py sendpatch FILE.syx --yes    # UNTESTED: send a whole patch (not saved)
python -B tools/g1on_sysex.py flashread ADDR LENGTH -o out.bin   # raw flash read
```

Options before the command: `--port NAME`, `--timeout S` (reply wait,
default 1 s), `-v` (print the SysEx traffic), `--stay-pcmode`.

Every run asks for the pedal's identity first and prints the device byte
and firmware (`Zoom device 0x66, firmware 1.30`). Every exchange is
logged as hex to `tools/probe_logs/`.

### Files: `list`, `usage`, `upload`, `delete`

- Names on the pedal are upper case; `upload` upper-cases a `.ZDL` name.
- `upload` refuses a file that is already on the pedal: **updating an
  effect means delete, then upload.** It also refuses when the flash has
  too few free blocks (files take 4,090-byte blocks). After writing it
  checks the size in the listing and prints `OK` or `FAIL`. A failed upload
  deletes its partial file.
- `delete` prints `OK` only when the pedal acked and the file is gone.
- Both need `--yes`.
- **At most 200 files** on the pedal, every file counted (also
  `FLST_SEQ.ZDT`). A 201st upload is refused before any data is sent.
- **A new effect type doesn't show up** until the effect list is rebuilt:
  upload all new effects, then delete `FLST_SEQ.ZDT` and restart the
  pedal. It writes a new list at the restart. Back that file up first if
  you want to keep your own effect order. Updating an effect that is
  already installed doesn't need this.
- These commands end with "PC mode off" (`53`), which reloads the current
  patch from flash: **save the patch first**, or unsaved edits are lost
  (`--stay-pcmode` leaves PC mode on).

### Patches: `patch`, `stored`, `knob`, `program`

- `patch` prints the current patch as the pedal sends it (editor mode
  `50`, then request `29`) and saves it with `-o`. It keeps unsaved edits.
  Below the raw message it prints the name, patch level, the effect id
  (gid, fxid) and on/off state of each slot, and the expression pedal's
  destination and range. `stored` prints the same.
- `stored N` prints saved patch N (0..99) without switching to it (editor
  mode `50`, then request `09 00 00 N`) and saves it with `-o`. It keeps
  unsaved edits.
- `knob SLOT PARAM VALUE` sets one parameter of the current patch live, as
  if the knob had been turned: slot 0..4, param 0 = on/off, 2..10 =
  knobs 1..9 (1 = effect type is refused). The value is the knob's step
  (0 .. its max). Nothing is saved; there is no reply (read it back with
  `patch`).
- `program N` switches to patch N (0..99) with a plain MIDI Program
  Change. Nothing is written to flash; unsaved edits are lost.
- **Before a replug or power-off, switch to a patch with stock effects
  only** (e.g. `program 0` if patch 0 holds no custom effects), so the
  pedal can't boot into an effect that fails to load.

### Pedal state: `current`, `settings`, `patchinfo`, `status`

All four only read; they keep unsaved edits.

- `current` prints the number of the current patch (editor mode `50`,
  then request `33`; the pedal answers with a MIDI Bank Select and
  Program Change).
- `settings` prints the pedal's global settings (editor mode `50`, then
  request `2B`): auto save, battery, LCD backlight and contrast, ECO,
  preselect, tuner output / calibration / type / flat, rhythm pattern and
  level, looper time and level, tempo, master level. Two fields are
  still unknown. These settings can't be written over USB: the pedal
  ignores the matching write message (`2A`).
- `patchinfo` prints the number of patches and the patch size (request `07`):
  `100 patches x 112 bytes`.
- `status` prints a status value (editor mode `50`, then request `16`);
  `5` on a B1Xon, its meaning is unknown.

### Untested: `tuner`, `sendpatch`

Never run on our pedals yet. Both come from SysExTones/g1on, a 2017 tool for
the original G1on (device 0x63, fw 1.21). Save the current patch first.

- `tuner on|off` sends MIDI CC 74 (64 = on, 0 = off) on channel 1, with an
  identity request before "on" and after "off", as the original tool does.
- `sendpatch FILE --yes` sends a whole patch to the edit buffer (`28` + the
  128 packed bytes, the format of the `29` reply). FILE is a `patch -o` or
  `stored -o` dump, or the 128 bytes alone. It replaces the current patch
  (unsaved edits are lost) and doesn't save it; it reads the patch back
  (`50` + `29`) and compares. In the G1Xon 1.21 firmware it only loads the
  patch like a patch switch, without writing flash, and the pedal answers
  `00 00`. It refuses to send while auto save is on, because the pedal then
  might store the patch itself. There is no check value, so a damaged file
  is loaded as it is (a power cycle loads the stored patch again).

### Flash read: `flashread`

`flashread ADDR LENGTH -o FILE` reads raw SPI flash (read-only, checked
with a CRC per chunk) in 768-byte chunks (`--chunk`). Larger replies get
lost on the way and can leave the pedal deaf until a replug.

`download` (reading a file by name) does **not** work on these pedals: the
pedal acks the request but never sends data. Use `flashread`.

## When the pedal stops answering

- After a few idle minutes, or after a large SysEx reply, the pedal can
  stop answering SysEx. Only a **physical replug** wakes it; restarting the
  Windows device doesn't.
- The tools send the identity request once. Further sends to a deaf pedal
  jam the Windows MIDI driver until a replug. A send that blocks for more
  than 3 s ends the run with `ERROR: SysEx send blocked ... replug the
  pedal`.
- Running a command under a timeout (e.g. `timeout 300 python -B ...`)
  keeps a stuck run from hanging the shell.

## `g1on_keepalive.py`

```
python -B tools/g1on_keepalive.py --interval 30
```

Keeps the pedal awake during a session: every `--interval` seconds
(default 60; 30 is safer) it opens the ports, sends one identity request
and closes them again, so `g1on_sysex.py` can run in between. A ping that
finds the ports busy is skipped. It prints `ok (ping N)` per answer and
**stops at the first missed reply** (replug the pedal, then start it
again), because further pings would jam the driver. Logs of failed pings
stay in `tools/probe_logs/`.

Stop it while another program needs the ports for long (Zoom Effect
Manager 2), and start it again right after: a gap of a minute or more
without SysEx can leave the pedal deaf.

## Protocol notes

What the tools send (`ZOOM` = `52 00 dd`, dd = the device byte):

| Message | Use |
|---|---|
| `7E 7F 06 01` | Identity request; the reply gives the device byte and firmware |
| `52` / `53` | PC mode on / off (`53` reloads the current patch) |
| `50` / `51` | Editor mode on / off (`51` reloads the current patch) |
| `60 25` / `60 26` / `60 27` | File search first / next / end (list) |
| `60 29` | Flash size and bytes available |
| `60 20 01 ...` + `60 23 ...` + close | Open for write, 512-byte chunks with CRC, close (upload) |
| `60 24 NAME` | Delete |
| `60 30 addr len` | Raw flash read |
| `29` | Current patch (answered in editor mode) |
| `09 00 00 N` | Saved patch N, reply `08 .. .. N .. ..` + patch |
| `31 slot param lo hi` | Set one parameter live (editor mode) |
| `33` | Current patch number, reply `B0 00 bank`, `B0 20 00`, `C0 N` (editor mode) |
| `2B` | Global settings, reply `2A` + 16 packed bytes (editor mode) |
| `07` | Patch count and size, reply `06 64 00 70 00` (100, 112) |
| `16` | Status, reply `17` + 5 bytes (editor mode) |
| `Cn prog` | MIDI Program Change: switch patch |

Messages the tools never send, because they write flash (read in the
G1Xon 1.21 firmware; `40` confirmed on a B1Xon 1.30): `42 00 00 N`
overwrites patch N with an empty patch; `40 lo hi` and `41` delete
effects from the current patch **and save it**.

`40 lo hi` (after `50`): `lo | hi << 7` is a slot mask, bit i deletes the
effect in slot i (slot 0 can't be deleted; a bit at or above the slot
count is refused with `00 0C`). The effects after a deleted one move up a
slot. The pedal first applies the current patch again (live knob values
included), deletes, saves the patch to its number and replies with a `28`
patch dump and `00 00`. `41` deletes every effect but slot 0 the same way.

The file messages follow
[mungewell/zoom-zt2](https://github.com/mungewell/zoom-zt2)'s protocol
for the newer pedals; the G1on family answers the same messages for
listing, upload and delete.
