"""File transfer for Zoom G1on/G1Xon-family pedals ("ZOOM 1 Series" MIDI ports).

Normal-mode (no firmware update) file protocol over USB MIDI SysEx. The device byte is auto-detected
(G1Xon fw 1.21 = 0x64). Every SysEx is sent from a worker thread with a timeout:
on Windows a send to a pedal that has stopped reading blocks in the driver until
the pedal is replugged, so we report it instead of hanging.

    python -B tools/zoom1_files.py list
    python -B tools/zoom1_files.py usage
    python -B tools/zoom1_files.py delete NAME.ZDL --yes
    python -B tools/zoom1_files.py upload path/NAME.ZDL --yes
    python -B tools/zoom1_files.py flashread 0xA5000 3231744 -o fs.bin   # raw flash (backup)
    python -B tools/zoom1_files.py download NAME.ZDL [-o DIR]      # does not work on G1Xon

STATUS (G1Xon fw 1.21): list, usage, delete, upload and flashread work.
`download` does NOT work: the pedal acks the read command but never sends data
(use flashread). Each run ends with PC mode off (53) unless --stay-pcmode is given.

Every exchange is logged as hex to tools/probe_logs/.
"""
from __future__ import annotations

import argparse
import binascii
import os
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

try:
    import mido
except ImportError:
    sys.exit("mido not installed: pip install mido python-rtmidi")

HERE = Path(__file__).resolve().parent
LOG_DIR = HERE / "probe_logs"
DEFAULT_OUT = HERE.parent / "pedal_backup"
ZOOM = 0x52


class SendBlocked(RuntimeError):
    pass


class Pedal:
    def __init__(self, port_substr: str, log_path: Path, timeout: float = 1.0):
        ins = [p for p in mido.get_input_names() if port_substr.lower() in p.lower()]
        outs = [p for p in mido.get_output_names() if port_substr.lower() in p.lower()]
        if not ins or not outs:
            sys.exit(f"No MIDI in/out port containing '{port_substr}'")
        self.inport = mido.open_input(ins[0])
        self.outport = mido.open_output(outs[0])
        self.timeout = timeout
        self.log = open(log_path, "w")
        self.log.write(f"in={ins[0]} out={outs[0]}\n")
        self.dev = None
        self.verbose = False

    def close(self):
        self.log.close()
        self.inport.close()
        self.outport.close()

    def _log(self, tag: str, data) -> None:
        line = f"{tag} F0 {bytes(data).hex(' ').upper()} F7"
        self.log.write(line + "\n")
        self.log.flush()
        if self.verbose:
            print("   " + line[:120])

    def xfer(self, data, quiet: float = 0.05) -> list:
        """Send one SysEx, return every SysEx reply (stops `quiet` s after the last one)."""
        for m in self.inport.iter_pending():
            if m.type == "sysex":
                self._log("<< (stray)", m.data)
        self._log(">>", data)
        done = threading.Event()

        def tx():
            self.outport.send(mido.Message("sysex", data=list(data)))
            done.set()

        threading.Thread(target=tx, daemon=True).start()
        if not done.wait(3.0):
            self.log.write("!! send blocked >3s\n")
            raise SendBlocked("SysEx send blocked in the MIDI driver -- replug the pedal")

        replies = []
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            got = False
            for m in self.inport.iter_pending():
                if m.type == "sysex":
                    self._log("<<", m.data)
                    replies.append(list(m.data))
                    got = True
            if got:
                deadline = min(deadline, time.monotonic() + quiet)
            time.sleep(0.003)
        if not replies:
            self.log.write("   (no reply)\n")
        return replies

    def cmd(self, *body, name: str | None = None) -> list:
        data = [ZOOM, 0x00, self.dev, *body]
        if name is not None:
            data += [ord(c) for c in name] + [0x00]
        return [r for r in self.xfer(data) if r[:3] == [ZOOM, 0x00, self.dev]]

    # --- session -----------------------------------------------------------

    def identify(self, tries: int = 1) -> int:
        # Send once and wait long. When the pedal is not reading SysEx, every extra
        # message queues in the Windows driver, and the second send blocks until a
        # USB replug, so retries make things worse.
        time.sleep(0.5)
        saved, self.timeout = self.timeout, max(self.timeout, 3.0)
        try:
            return self._identify(tries)
        finally:
            self.timeout = saved

    def _identify(self, tries: int) -> int:
        for attempt in range(tries):
            for r in self.xfer([0x7E, 0x7F, 0x06, 0x01]):
                if r[:4] == [0x7E, r[1], 0x06, 0x02] and len(r) > 5 and r[4] == ZOOM:
                    self.dev = r[5]
                    ver = bytes(r[9:13]).decode("ascii", "replace")
                    print(f"Zoom device 0x{self.dev:02X}, firmware {ver}"
                          + (f" (attempt {attempt + 1})" if attempt else ""))
                    return self.dev
        raise RuntimeError("no Zoom identity reply (try replugging the pedal)")

    def pcmode(self, on: bool) -> None:
        self.cmd(0x52 if on else 0x53)   # not acked on G1Xon fw 1.21

    # --- file protocol (zoomzt2.py) -----------------------------------------

    def find(self, first: bool, pattern: str = "*"):
        for r in self.cmd(0x60, 0x25 if first else 0x26, 0x00, 0x00, name=pattern):
            if len(r) >= 34 and r[4] == 0x04:
                name = bytes(r[14:29]).split(b"\0")[0].decode("ascii", "replace")
                size = sum(r[29 + i] << (7 * i) for i in range(5))
                return name, size
        return None

    def find_close(self) -> None:
        self.cmd(0x60, 0x27)

    def listing(self):
        out = []
        hit = self.find(True)
        while hit and len(out) < 1000:
            out.append(hit)
            hit = self.find(False)
        self.find_close()
        return out

    def file_close(self) -> None:
        # two close messages (zoomzt2.py sends one byte fewer in each)
        self.cmd(0x60, 0x21, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00)
        self.cmd(0x60, 0x09, 0x00)

    def disk_usage(self):
        """(max_bytes, available_bytes) or None."""
        for r in self.cmd(0x60, 0x29):
            if len(r) >= 20 and r[4] == 0x04:
                def u35(b):
                    return sum(int(x) << (7 * i) for i, x in enumerate(b))
                return u35(r[10:15]), u35(r[15:20])
        return None

    def flash_read(self, addr: int, length: int) -> bytes:
        """Raw SPI-flash read, G1on OS sub-command 0x30 (handler 0xC0090328 -> FAST_READ 0x0B).

        Read-only: verified in the OS disassembly (no write-enable/program/erase on that
        path). Reply: 60 04 30 <seq 2B> <len 2B, 14-bit> <7-bit packed data> <CRC32 5B>.
        """
        if not 0 < length <= 0x1000 or addr + length > 0x400000:
            raise ValueError("length 1..4096 within the 4 MiB flash")
        for r in self.cmd(0x60, 0x30, *self._u35(addr), *self._u35(length)):
            if len(r) > 15 and r[4] == 0x04 and r[5] == 0x30:
                n = r[8] | (r[9] << 7)
                block = self._unpack(r[10:-5])[:n]
                crc = r[-5] | (r[-4] << 7) | (r[-3] << 14) | (r[-2] << 21) | ((r[-1] & 0x0F) << 28)
                if (crc ^ 0xFFFFFFFF) != binascii.crc32(bytes(block)):
                    raise IOError(f"flash read @{addr:#x}: CRC mismatch")
                if n != length:
                    raise IOError(f"flash read @{addr:#x}: got {n} bytes, asked {length}")
                return bytes(block)
        raise IOError(f"flash read @{addr:#x}: no data reply")

    CHECK_OK = [0x60, 0x03, 0, 0, 0, 0, 0]   # status / delete OK reply

    def upload(self, name: str, data: bytes, progress=None) -> None:
        """Write a file. Raises IOError (after cleanup) on any bad reply."""
        self.cmd(0x60, 0x27)                                          # file mode (ends a search)
        op = self.cmd(0x60, 0x20, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, name=name)   # open for write
        if not op or op[0][4] != 0x04:
            raise IOError(f"open for write refused: {[bytes(r).hex(' ') for r in op]}")
        try:
            for off in range(0, len(data), 512):
                chunk = data[off:off + 512]
                chk = self.cmd(0x60, 0x05, 0x00)
                if not any(r[3:] == self.CHECK_OK for r in chk):
                    raise IOError(f"status check before offset {off} failed: {[bytes(r).hex(' ') for r in chk]}")
                crc = binascii.crc32(chunk) ^ 0xFFFFFFFF
                body = ([0x60, 0x23, 0x40, 0, 0, 0, 0] + self._u35(len(chunk))
                        + list(self._pack(chunk)) + self._u35(crc) + [0x00])
                rep = self.cmd(*body)
                if not rep or rep[0][4] != 0x04:
                    raise IOError(f"write at offset {off} refused: {[bytes(r).hex(' ') for r in rep]}")
                if progress:
                    progress(off + len(chunk), len(data))
        except Exception:
            self.file_close()
            time.sleep(0.2)
            self.delete(name)   # remove the partial file
            raise
        self.file_close()

    @staticmethod
    def _u35(v: int) -> list:
        return [(v >> s) & 0x7F for s in (0, 7, 14, 21)] + [(v >> 28) & 0x0F]

    @staticmethod
    def _pack(data: bytes) -> bytearray:
        # 7-bit groups, header byte holds the MSBs, bit 6 = first data byte (as zoomzt2)
        out = bytearray()
        for g in range(0, len(data), 7):
            grp = data[g:g + 7]
            out.append(sum(((b >> 7) & 1) << (6 - i) for i, b in enumerate(grp)))
            out += bytes(b & 0x7F for b in grp)
        return out

    def delete(self, name: str) -> bool:
        """Delete a file: one message, success = the delete OK reply."""
        replies = self.cmd(0x60, 0x24, name=name)
        return any(r[3:] == [0x60, 0x03, 0, 0, 0, 0, 0] for r in replies)

    @staticmethod
    def _data_block(r):
        """Return the payload length if r looks like a data reply, else None.

        Status 04, 7-bit length at [8..9], packed data from [10], 5-byte CRC at the end.
        A short ack such as `60 04 22 00 00 04 00 ...` has the same header, so the
        message length must match the declared payload exactly.
        """
        if len(r) < 16 or r[4] != 0x04:
            return None
        length = r[9] * 128 + r[8]
        packed = length + (length + 6) // 7
        return length if length and len(r) == 10 + packed + 5 else None

    # zoomzt2.py's read command for the G1Four; bytes 2..5 look like a per-open
    # handle (G1Xon returns its own in the open reply at [11:15]).
    G1FOUR_READ_HANDLE = [0x14, 0x2F, 0x60, 0x00]

    def download(self, name: str, handle_from_open: bool = True) -> bytes:
        hit = self.find(True, name)
        self.find_close()
        if not hit:
            raise FileNotFoundError(name)
        expect = hit[1]

        op = self.cmd(0x60, 0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, name=name)   # open for read
        if handle_from_open and op and len(op[0]) >= 15:
            handle = op[0][11:15]
        else:
            handle = self.G1FOUR_READ_HANDLE
        data = bytearray()
        blocks = 0
        try:
            while True:
                self.cmd(0x60, 0x05, 0x00)
                replies = self.cmd(0x60, 0x22, *handle, 0x0C, 0x00, 0x04, 0x00, 0x00, 0x00)
                replies += self.cmd(0x60, 0x05, 0x00)
                blk = next((r for r in replies if self._data_block(r)), None)
                if blk is None:
                    break
                length = self._data_block(blk)
                block = self._unpack(blk[10:-5])[:length]
                crc = blk[-5] | (blk[-4] << 7) | (blk[-3] << 14) | (blk[-2] << 21) | ((blk[-1] & 0x0F) << 28)
                if (crc ^ 0xFFFFFFFF) != binascii.crc32(bytes(block)):
                    raise IOError(f"{name}: CRC mismatch in block {blocks}")
                data += block
                blocks += 1
                if len(data) >= expect:
                    break
        finally:
            self.file_close()
        if len(data) != expect:
            raise IOError(f"{name}: got {len(data)} bytes, listing says {expect}")
        return bytes(data)

    @staticmethod
    def _unpack(packet) -> bytearray:
        # 7-bit groups: header byte holds the MSBs, bit 6 = first data byte
        out = bytearray()
        loop = -1
        hibits = 0
        for b in packet:
            if loop != -1:
                out.append(b | 0x80 if hibits & (1 << loop) else b)
                loop -= 1
            else:
                hibits = b
                loop = 6
        return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="ZOOM 1 Series")
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("-v", "--verbose", action="store_true", help="echo SysEx traffic")
    ap.add_argument("--stay-pcmode", action="store_true",
                    help="do not send PC mode off (53) at the end")
    sub = ap.add_subparsers(dest="action", required=True)
    sub.add_parser("list")
    sub.add_parser("usage")
    rm = sub.add_parser("delete", help="delete files on the pedal (destructive)")
    rm.add_argument("names", nargs="+")
    rm.add_argument("--yes", action="store_true", help="required: confirms the pedal content is backed up")
    up = sub.add_parser("upload", help="write files to the pedal (normal mode)")
    up.add_argument("files", nargs="+", type=Path)
    up.add_argument("--yes", action="store_true", help="required: confirms the pedal content is backed up")
    fr = sub.add_parser("flashread", help="read raw SPI flash (read-only, sub-command 0x30)")
    fr.add_argument("addr", type=lambda s: int(s, 0))
    fr.add_argument("length", type=lambda s: int(s, 0))
    fr.add_argument("-o", "--out", type=Path, required=True)
    # A 4 KB read (~4.7 KB reply) never arrived through the Windows MIDI input
    # (likely RtMidi's 1 KB SysEx buffer), and the pedal was deaf afterwards until a
    # replug (likely its stuck USB transmit). 768 B -> ~890 B replies.
    fr.add_argument("--chunk", type=lambda s: int(s, 0), default=768)
    dl = sub.add_parser("download")
    dl.add_argument("names", nargs="+")
    dl.add_argument("-o", "--outdir", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    LOG_DIR.mkdir(exist_ok=True)
    log_path = LOG_DIR / f"{args.action}_{datetime.now():%Y%m%d_%H%M%S}.log"
    pedal = Pedal(args.port, log_path, args.timeout)
    pedal.verbose = args.verbose
    rc = 0
    try:
        pedal.identify()
        pedal.pcmode(True)
        try:
            if args.action == "list":
                files = pedal.listing()
                for name, size in files:
                    print(f"{size:8d}  {name}")
                print(f"{len(files)} files, {sum(s for _, s in files)} bytes")
            elif args.action == "upload":
                if not args.yes:
                    sys.exit("upload writes flash: re-run with --yes once the pedal is backed up")
                for path in args.files:
                    data = path.read_bytes()
                    name = path.name.upper() if path.suffix.upper() == ".ZDL" else path.name
                    if pedal.find(True, name):
                        pedal.find_close()
                        print(f"SKIP {name}: already on pedal (delete it first)")
                        rc = 1
                        continue
                    pedal.find_close()
                    du = pedal.disk_usage()
                    need = -(-len(data) // 4090) * 4090
                    if du and du[1] < need:
                        print(f"SKIP {name}: needs {need} bytes, {du[1]} available")
                        rc = 1
                        continue
                    t0 = time.monotonic()
                    try:
                        pedal.upload(name, data, progress=lambda d, t: print(f"\r   {d}/{t}", end="", flush=True))
                        print()
                    except IOError as e:
                        print(f"\nFAIL {name}: {e} (partial file deleted)")
                        rc = 1
                        continue
                    hit = pedal.find(True, name)
                    pedal.find_close()
                    after = pedal.disk_usage()
                    ok = bool(hit) and hit[1] == len(data)
                    print(f"{'OK  ' if ok else 'FAIL'} {name}: wrote {len(data)} bytes in {time.monotonic() - t0:.1f}s, "
                          f"listing says {hit[1] if hit else 'missing'}, available now {after[1] if after else '?'}")
                    if not ok:
                        rc = 1
                pedal.identify(tries=1)
            elif args.action == "flashread":
                out = bytearray()
                t0 = time.monotonic()
                for a in range(args.addr, args.addr + args.length, args.chunk):
                    n = min(args.chunk, args.addr + args.length - a)
                    out += pedal.flash_read(a, n)
                    print(f"\r   {len(out)}/{args.length}", end="", flush=True)
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_bytes(out)
                print(f"\nOK   {len(out)} bytes from {args.addr:#x} in {time.monotonic() - t0:.1f}s -> {args.out}")
            elif args.action == "usage":
                print("disk (max, available):", pedal.disk_usage())
            elif args.action == "delete":
                if not args.yes:
                    sys.exit("delete is destructive: re-run with --yes once the pedal is backed up")
                for name in args.names:
                    hit = pedal.find(True, name)
                    pedal.find_close()
                    if not hit:
                        print(f"SKIP {name}: not on pedal")
                        continue
                    before = pedal.disk_usage()
                    ok = pedal.delete(name)
                    gone = pedal.find(True, name) is None
                    pedal.find_close()
                    after = pedal.disk_usage()
                    print(f"{'OK  ' if ok and gone else 'FAIL'} {name} ({hit[1]} bytes): "
                          f"ack={'good' if ok else 'unexpected'}, gone={gone}, "
                          f"available {before[1] if before else '?'} -> {after[1] if after else '?'}")
                    if not (ok and gone):
                        rc = 1
                pedal.identify(tries=1)   # still talking to us?
            else:
                args.outdir.mkdir(parents=True, exist_ok=True)
                for name in args.names:
                    t0 = time.monotonic()
                    try:
                        data = pedal.download(name)
                    except (IOError, FileNotFoundError) as e:
                        print(f"FAIL {name}: {e}")
                        rc = 1
                        continue
                    path = args.outdir / name
                    path.write_bytes(data)
                    print(f"OK   {name}: {len(data)} bytes, CRC ok, {time.monotonic() - t0:.1f}s -> {path}")
        finally:
            # Leave PC mode so the pedal returns to its normal screen. (53 was once
            # suspected of making the pedal deaf, but deaf spells also happened
            # without it.) G1Xon fw 1.21 does not ack 52/53.
            if not args.stay_pcmode:
                pedal.pcmode(False)
    except SendBlocked as e:
        print(f"ERROR: {e}\nLog: {log_path}")
        os._exit(2)   # a blocked driver thread would keep the process alive
    except BaseException as e:
        # Any other failure (no identity reply, Ctrl+C, ...): release the MIDI
        # ports before exiting instead of leaving it to process teardown.
        pedal.close()
        if isinstance(e, RuntimeError):
            print(f"ERROR: {e}\nLog: {log_path}")
            sys.exit(1)
        raise
    pedal.close()
    print(f"Log: {log_path}")
    sys.exit(rc)


if __name__ == "__main__":
    main()
