"""Keep a G1Xon answering SysEx between tool runs (and measure when it goes deaf).

The pedal stops answering SysEx after some idle minutes; only a physical
replug brings it back (a Windows device restart via pnputil does not:
the deafness is in the pedal, not the driver). This sends one
identity request every --interval seconds so the pedal never sits idle.

Each ping opens the ports, sends once and closes them again, so
zoom1_files.py can run in between (Windows MIDI ports are exclusive: a ping
that finds them busy is skipped). The script stops at the FIRST missed reply:
further sends to a deaf pedal queue in the driver and jam it until a replug.

    python -B tools/zoom1_keepalive.py               # ping every 60 s, until Ctrl+C
    python -B tools/zoom1_keepalive.py --interval 30

Every exchange is logged to tools/probe_logs/keepalive_*.log.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import sys
import time
from datetime import datetime

from zoom1_files import LOG_DIR, Pedal, SendBlocked


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--interval", type=float, default=60.0, help="seconds between pings")
    ap.add_argument("--port", default="ZOOM 1 Series")
    a = ap.parse_args()

    LOG_DIR.mkdir(exist_ok=True)
    start = time.monotonic()
    last_ok = None
    n = 0
    while True:
        now = datetime.now()
        log_path = LOG_DIR / f"keepalive_{now:%Y%m%d_%H%M%S}.log"
        try:
            pedal = Pedal(a.port, log_path, timeout=3.0)
        except (Exception, SystemExit) as e:   # ports busy (zoom1_files running) or gone
            print(f"{now:%H:%M:%S}  skip: {e}", flush=True)
            time.sleep(a.interval)
            continue
        try:
            with contextlib.redirect_stdout(io.StringIO()):   # mute "Zoom device ..."
                ok = bool(pedal._identify(1))
        except SendBlocked as e:
            print(f"{now:%H:%M:%S}  ERROR: {e}", flush=True)
            os._exit(2)                 # a blocked driver thread would keep us alive
        except RuntimeError:
            ok = False
        pedal.close()
        n += 1
        if not ok:
            since = f"{time.monotonic() - last_ok:.0f} s after the last reply" if last_ok else "on the first ping"
            print(f"{now:%H:%M:%S}  NO REPLY ({since}, ping {n}, "
                  f"{(time.monotonic() - start) / 60:.1f} min running). Stopping: replug the pedal.",
                  flush=True)
            sys.exit(1)
        log_path.unlink()               # keep probe_logs/ for failures only
        last_ok = time.monotonic()
        print(f"{now:%H:%M:%S}  ok (ping {n})", flush=True)
        time.sleep(a.interval)


if __name__ == "__main__":
    main()
