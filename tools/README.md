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
| `g1on_sysex.py` | List, upload and delete effect files; disk usage; raw flash read; read the current patch; set a knob live; switch patches |
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
python -B tools/g1on_sysex.py knob SLOT PARAM VALUE       # set a knob of the current patch live
python -B tools/g1on_sysex.py program N                   # switch to patch N (0..99)
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

### Patches: `patch`, `knob`, `program`

- `patch` prints the current patch as the pedal sends it (editor mode
  `50`, then request `29`) and saves it with `-o`. It keeps unsaved edits.
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
| `31 slot param lo hi` | Set one parameter live (editor mode) |
| `Cn prog` | MIDI Program Change: switch patch |

The file messages follow
[mungewell/zoom-zt2](https://github.com/mungewell/zoom-zt2)'s protocol
for the newer pedals; the G1on family answers the same messages for
listing, upload and delete.
