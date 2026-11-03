#!/usr/bin/env python3
"""Give a sampled drum a real-sample start: the WAV's first
part, low-passed and decimated to 14,700 Hz, coded as adaptive DPCM with
2 or 3 bits per sample, written into drums/<id>.json as its "sample"
field. The DSP decodes it while the drum plays (drumbox.c db_smp_*,
preview.py, kits.smp_decode):

  pred = a * y; code = magnitude k | sign << (bits - 1), LSB-first bit
  stream; y = pred +- (k + 0.5) * step; step *= mult[k], clamped to
  STEP_MIN..STEP_MAX; played back at 44.1 kHz by linear interpolation
  (DIV = 3), times gain * vel, faded out linearly from grid point xf_ms[0]
  to xf_ms[1] (both GRID_MS points)

kits.load_drums zeroes the drum's band rows below the sample's top
(bands under SMP_HI_HZ) and its partials up to xf_ms[0], so the resynthesis
body fades in over the same grid segment while the sample fades out; the
bands above play under the sample all along (it has nothing above 7 kHz).

"gain" and the drum's "level" are both multiplied by the lab's loudness
match (the old drum's peak 20 ms RMS), so the drum keeps its loudness.
Keep a copy of the drum file: a drum that has a sample is refused.

Usage: python -B drum_sample.py <id> <wav> <bits> <xf_start_ms> <xf_end_ms> <match>
"""
from __future__ import annotations

import json
import math
import struct
import sys
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from kits import (dump_drum, GRID_MS, SR, SMP_DIV as DIV, SMP_MULT as MULT,  # noqa: E402
                  SMP_STEP_MIN as STEP_MIN, SMP_STEP_MAX as STEP_MAX)


def read_mono(path):
    w = wave.open(str(path), "rb")
    n, ch = w.getnframes(), w.getnchannels()
    v = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    x = [sum(v[i * ch:(i + 1) * ch]) / (ch * 32768.0) for i in range(n)]
    on = next(i for i, s in enumerate(x) if abs(s) > 0.01)
    return x[on:]


def lowpass_decimate(x, d, taps=63):
    """Windowed-sinc low-pass at 0.45 * the new rate, keep every d-th sample."""
    fc = 0.45 / d
    h = []
    for i in range(taps):
        t = i - (taps - 1) / 2
        s = 2 * fc if t == 0 else math.sin(2 * math.pi * fc * t) / (math.pi * t)
        h.append(s * (0.42 - 0.5 * math.cos(2 * math.pi * i / (taps - 1))
                      + 0.08 * math.cos(4 * math.pi * i / (taps - 1))))
    g = sum(h)
    h = [c / g for c in h]
    half = (taps - 1) // 2
    out = []
    for n in range(0, len(x), d):
        acc = 0.0
        for k, c in enumerate(h):
            j = n + half - k
            if 0 <= j < len(x):
                acc += c * x[j]
        out.append(acc)
    return out


def encode(x, bits):
    """Closed-loop adaptive DPCM -> (a, step0, codes)."""
    r0 = sum(s * s for s in x) or 1e-12
    r1 = sum(x[i] * x[i - 1] for i in range(1, len(x)))
    a = max(-0.95, min(0.95, r1 / r0))
    levels = 1 << (bits - 1)
    mult = MULT[bits]
    step0 = step = max(1e-4, abs(x[0]) / levels)
    y, codes = 0.0, []
    for s in x:
        pred = a * y
        e = (s - pred) / step
        k = min(levels - 1, int(abs(e)))
        codes.append(k | ((1 if e < 0 else 0) << (bits - 1)))
        y = pred + (k + 0.5) * (-1 if e < 0 else 1) * step
        step = max(STEP_MIN, min(STEP_MAX, step * mult[k]))
    return a, step0, codes


def pack(codes, bits):
    """LSB-first bit stream + one padding byte (the DSP reads 2 bytes)."""
    out = bytearray((len(codes) * bits + 7) // 8 + 1)
    p = 0
    for c in codes:
        for i in range(bits):
            if c >> i & 1:
                out[(p + i) >> 3] |= 1 << ((p + i) & 7)
        p += bits
    return bytes(out)


def main():
    did, wav, bits, xa, xb, match = sys.argv[1:7]
    bits, xa, xb, match = int(bits), int(xa), int(xb), float(match)
    if xa not in GRID_MS or xb not in GRID_MS or xb <= xa:
        sys.exit("xf points must be GRID_MS points, start < end")
    end = int(round(xb * SR / 1000.0))
    n = (end + DIV - 1) // DIV + 2              # decimated samples to decode
    x = read_mono(HERE / wav)
    lo = lowpass_decimate(x[:n * DIV + 64], DIV)[:n]
    a, step0, codes = encode(lo, bits)
    path = HERE / "drums" / f"{did}.json"
    m = json.loads(path.read_text())
    if "sample" in m:
        sys.exit(f"{path.name} already has a sample (restore the file without it first)")
    m["level"] = round(m["level"] * match, 6)
    m["sample"] = {"wav": wav, "div": DIV, "bits": bits, "xf_ms": [xa, xb],
                   "gain": round(match, 6), "a": a, "step0": step0, "n": n,
                   "data": pack(codes, bits).hex()}
    m["edit"] = (m.get("edit", "") + "; " if m.get("edit") else "") + \
        f"drum_sample.py: {bits}-bit sample to {xb} ms, level x {match:.4f}"
    path.write_text(dump_drum(m))
    print(f"{did}: {n} samples, {len(m['sample']['data']) // 2} B, a {a:.3f}")


if __name__ == "__main__":
    main()
