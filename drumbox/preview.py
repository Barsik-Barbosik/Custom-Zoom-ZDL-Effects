#!/usr/bin/env python3
"""Desktop preview: render DrumBox beats + fills to WAV without the pedal.

Line-for-line mirror of the engine (db_hit / db_render), the kit cache
(each lane's drum rendered once, played back x velocity) and the sequencer
in drumbox.c -- keep in sync when the C DSP changes. Drums come
from kits.py (drums/: sampled models, and synth drums whose Python hit() /
render() mirror their drums/<algo>.c; the same data the C header is made
from).

Beats and fills are given by knob value or screen name. The song plays
section A (BeatA, LenA bars, FillA on its last bar), then section B
(BeatB, LenB bars, FillB), and repeats.

Usage:
  python -B preview.py                      # all BeatAs, default rest -> preview/
  python -B preview.py --beat-a Rock --fill-a Sp1 --beat-b OHRk --fill-b Tm2 --bpm 96
  python -B preview.py --beat-a Rock --fill-b all   # one WAV per FillB
  python -B preview.py --beat-a 33 --kit Synth     # kit by index, name or short name
  python -B preview.py --beat-a Rock --kit all     # one WAV per kit
  python -B preview.py --beat-a Rock --knob 5      # as the Kit knob at 5
  python -B preview.py --beat-a Rock --len-a 2Bar --len-b 1Bar --bars 12
"""

from __future__ import annotations

import argparse
import struct
import wave
from pathlib import Path

from kits import (BANDS, CHOKE_SAMPLES, GRID, GRID_INV, KITS, MAX_PARTIALS, NUM_BANDS,
                  NUM_SEG, SMP_DIV, SMP_STEP_MAX, SMP_STEP_MIN, amp_of_code, kit_index,
                  knob_to_kit)
from synth_lib import INV_SR, SR, SynLane, State, db_sin, frac, lerp, soft_clip, wrap  # noqa: F401
from patterns import (BEATS, FILLS, LENGTHS, DEFAULT_BEAT_B, DEFAULT_FILL_A, DEFAULT_FILL_B,
                      DEFAULT_LEN_A, DEFAULT_LEN_B, FILL_RNG_SEED,
                      bar_swing, beat_index, fill_index, masks_of, section_fill,
                      length_index, resolve_fill)
from patterns import LANES as PLANES, BEAT_RANDOM, BEAT_RNG_SEED, resolve_beat

# engine lanes 0..7 (kick .. high tom, low tom) as indices into patterns.LANES
SOUND_LANES = [PLANES.index(c) for c in "KSHORCTL"]
ACCENT = PLANES.index("A")


SYN_RNG_SEED, SYN_RNG_STEP = 0x1234567, 0x9E3779B9    # DB_SYN_RNG_SEED / _STEP


class Lane:
    """Engine state of one pattern lane (the drum the kit puts there)."""

    def __init__(self, i=0):
        self.active, self.t, self.seg, self.vel = False, 0, 0, 0.0
        self.model = None           # the drum it was hit with
        self.syn = SynLane((SYN_RNG_SEED + i * SYN_RNG_STEP) & 0xFFFFFFFF)
        self.bcur = [0.0] * NUM_BANDS
        self.bslope = [0.0] * NUM_BANDS
        self.pcur = [0.0] * MAX_PARTIALS
        self.pslope = [0.0] * MAX_PARTIALS
        self.ph = [0.0] * MAX_PARTIALS
        self.pd = [0.0] * MAX_PARTIALS
        self.smp = None             # real-sample start playing (kits m["smp"])
        self.sPos = self.sSub = self.sBit = 0
        self.sYA = self.sYB = self.sStep = 0.0


def smp_next(s):
    """Same as db_smp_next() in drumbox.c."""
    smp = s.smp
    bits, p, data = smp["bits"], s.sBit, smp["data"]
    w = data[p >> 3] | (data[(p >> 3) + 1] << 8)
    half = 1 << (bits - 1)
    c = (w >> (p & 7)) & (2 * half - 1)
    k = c & (half - 1)
    q = k + 0.5
    y = smp["a"] * s.sYB + (-q if c & half else q) * s.sStep
    s.sBit = p + bits
    s.sStep = min(SMP_STEP_MAX, max(SMP_STEP_MIN, s.sStep * smp["mult"][k]))
    return y


def _band_val(m, b, k, vel):
    """Target amplitude of band b at grid point k + 1 (0 outside the range)."""
    if m["band_lo"] <= b <= m["band_hi"]:
        return m["band_scale"] * amp_of_code(m["band_env"][b - m["band_lo"]][k]) * vel
    return 0.0


def _part_val(m, i, k, vel):
    if i < len(m["partials"]):
        return m["part_scale"] * amp_of_code(m["partials"][i]["env"][k]) * vel
    return 0.0


def _new_state(code):
    """A synth drum's state words, all zero (as db_init_state leaves them)."""
    st = State()
    for name, n in code.STATE.items():
        setattr(st, name, 0.0 if n == 1 else [0.0] * n)
    return st


class Voice:
    """Line-for-line mirror of the engine in drumbox.c (db_hit, db_choke,
    db_render): per lane, partials + noise-band envelopes; one band-pass
    bank on two noise sources shared by all lanes (see kits.py)."""

    def __init__(self):
        self.rngA, self.rngB = 0x1234567, 0x2C9277B5
        self.xa1 = self.xa2 = self.xb1 = self.xb2 = 0.0
        self.y1 = [0.0] * NUM_BANDS
        self.y2 = [0.0] * NUM_BANDS
        self.lanes = [Lane(i) for i in range(len(SOUND_LANES))]

    @staticmethod
    def _xorshift(x):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        return x, (x - 0x100000000 if x >= 0x80000000 else x) * 4.6566129e-10

    def hit(self, lane, m, vel):
        s = self.lanes[lane]
        s.model = m
        # the lane is fresh (a new Voice per cached drum, db_reset_engine)
        if m["engine"] == "synth":
            v = s.syn
            v.st = _new_state(m["code"])        # C: zeroed state words
            v.algo = m["algo"]
            v.active = True
            m["code"].hit(v, m["params"], vel)
            return
        s.active, s.vel = True, vel
        s.smp = m.get("smp")
        if s.smp is not None:
            s.sPos = s.sSub = s.sBit = 0
            s.sYA = s.sYB = 0.0
            s.sStep = s.smp["step0"]
        for b in range(m["band_lo"], m["band_hi"] + 1):
            s.bslope[b] = _band_val(m, b, 0, vel) * GRID_INV[0]
        for i in range(MAX_PARTIALS):
            s.pslope[i] = _part_val(m, i, 0, vel) * GRID_INV[0]
            if i < len(m["partials"]):
                s.pd[i] = m["partials"][i]["f_delta"]

    def render(self, kp):
        self.rngA, na = self._xorshift(self.rngA)
        self.rngB, nb = self._xorshift(self.rngB)
        da, db = na - self.xa2, nb - self.xb2
        self.xa2, self.xa1 = self.xa1, na
        self.xb2, self.xb1 = self.xb1, nb
        g = [0.0] * NUM_BANDS
        out = 0.0
        for lane, s in enumerate(self.lanes):
            m = s.model
            if m is not None and m["engine"] == "synth":
                v = s.syn
                if not v.active:
                    continue
                y = m["code"].render(v, m["params"])
                out += y * m["level"]
                continue
            if not s.active:
                continue
            if s.smp is not None:
                smp, pos = s.smp, s.sPos
                y = s.sYA + (s.sYB - s.sYA) * s.sSub * (1.0 / SMP_DIV)
                gn = 1.0 if pos < smp["fa"] else (smp["fb"] - pos) * smp["inv"]
                out += y * smp["gain"] * s.vel * gn
                s.sSub += 1
                if s.sSub == SMP_DIV:
                    s.sSub = 0
                    s.sYA = s.sYB
                    s.sYB = smp_next(s)
                pos += 1
                s.sPos = pos
                if pos >= smp["fb"]:
                    s.smp = None
            for b in range(m["band_lo"], m["band_hi"] + 1):
                s.bcur[b] += s.bslope[b]
                g[b] += s.bcur[b]
            for i, p in enumerate(m["partials"]):
                s.pd[i] *= p["glide"]
                s.ph[i] = wrap(s.ph[i] + (p["f_end"] + s.pd[i]) * INV_SR)
                s.pcur[i] += s.pslope[i]
                out += s.pcur[i] * db_sin(s.ph[i])
            s.t += 1
            if s.t >= GRID[s.seg + 1]:
                s.seg += 1
                k = s.seg
                if k >= NUM_SEG:
                    s.active = False
                    continue
                for b in range(m["band_lo"], m["band_hi"] + 1):
                    s.bcur[b] = _band_val(m, b, k - 1, s.vel)
                    s.bslope[b] = (_band_val(m, b, k, s.vel) - s.bcur[b]) * GRID_INV[k]
                for i in range(len(m["partials"])):
                    s.pcur[i] = _part_val(m, i, k - 1, s.vel)
                    s.pslope[i] = (_part_val(m, i, k, s.vel) - s.pcur[i]) * GRID_INV[k]
        for b in range(NUM_BANDS):
            b0, a1, a2 = BANDS[b]
            y = b0 * (da if b % 2 == 0 else db) - a1 * self.y1[b] - a2 * self.y2[b]
            self.y2[b] = self.y1[b]
            self.y1[b] = y
            out += g[b] * y
        return out


CRASH_GAP = 4   # DB_CRASH_GAP: bars from one downbeat crash to the next, at least

# cache samples rendered per audio call (8 samples): the pedal renders for
# DB_RENDER_CYC TSCL cycles, which depends on the drum; ~8 is a guess
RENDER_N = 8
QUIET = 4410        # DB_QUIET: a drum ends after this many samples under LOUD
LOUD = 2.0          # DB_LOUD, in cache units (1 / CACHE_ONE)
CACHE_ONE = 16384.0
# DB_RENDER_LANE: lanes in render order, closed hat first (the count-in)
RENDER_ORDER = [2, 0, 1, 3, 4, 5, 6, 7]
# ctx[3] on the G1Xon: 704 KB (measured) minus the state, as
# int16 samples (approximate: the C takes what is left after DBState)
CACHE_CAP = (704 * 1024 - 8192) // 2


def _engine_done(s):
    return not s.active and not s.syn.active


class KitCache:
    """The kit cache in drumbox.c: each lane's drum rendered once at
    velocity 1 by a fresh engine (db_reset_engine), RENDER_N samples per
    call, stored as int16 (x CACHE_ONE, truncated), its quiet tail cut;
    lanes in RENDER_ORDER, ready ones in ready (rMask)."""

    def __init__(self, kp, cap=CACHE_CAP):
        self.kp, self.cap = kp, cap
        self.r_lane = self.r_pos = self.r_end = self.r_last = 0
        self.ready = 0
        self.c_off = [0] * len(SOUND_LANES)
        self.c_len = [0] * len(SOUND_LANES)
        self.data = []
        self.v = None

    def render_n(self, n=RENDER_N):
        i = 0
        while i < n and self.r_lane < len(SOUND_LANES):
            i += 1
            lane = RENDER_ORDER[self.r_lane]
            m = self.kp["drums"][lane]
            at = self.r_end + self.r_pos
            if self.r_pos == 0:
                self.v = Voice()                    # db_reset_engine
                self.r_last = -1
                self.c_off[lane] = self.r_end
                if m is not None:
                    self.v.hit(lane, m, 1.0)
            if (m is None or at >= self.cap or self.r_pos - self.r_last > QUIET or
                    (self.r_pos > 0 and _engine_done(self.v.lanes[lane]))):
                self.c_len[lane] = self.r_last + 1
                self.r_end += self.r_last + 1
                del self.data[self.r_end:]          # C: overwritten by the next lane
                self.r_lane += 1
                self.ready |= 1 << lane
                self.r_pos = 0
                continue
            y = max(-32767.0, min(32767.0, self.v.render(self.kp) * CACHE_ONE))
            self.data.append(int(y))
            if abs(y) >= LOUD:
                self.r_last = self.r_pos
            self.r_pos += 1


class Players:
    """Two players per lane (db_trigger_step / the players loop in
    drumbox.c): a hit starts the lane's other player on its cached drum x
    velocity, the one that was playing fades out in CHOKE_SAMPLES; a closed
    hat fades the open hat's."""

    def __init__(self):
        n = len(SOUND_LANES)
        self.cur = [0] * n
        self.idx = [0] * (2 * n)
        self.rem = [0] * (2 * n)
        self.gain = [0.0] * (2 * n)
        self.step = [0.0] * (2 * n)

    def trigger(self, cache, kp, masks, step, vel_lo=0.72):
        bit = 1 << (step & 15)
        vel = 1.0 if masks[ACCENT] & bit else vel_lo
        hits = [bool(masks[mi] & bit) and bool(cache.ready & (1 << lane))
                for lane, mi in enumerate(SOUND_LANES)]
        if hits[2]:
            v = 6 + self.cur[3]
            self.step[v] = -self.gain[v] * (1.0 / CHOKE_SAMPLES)
        for lane in range(len(SOUND_LANES)):
            if not hits[lane] or kp["drums"][lane] is None:
                continue
            v = 2 * lane + self.cur[lane]
            self.step[v] = -self.gain[v] * (1.0 / CHOKE_SAMPLES)
            v ^= 1
            self.cur[lane] ^= 1
            self.idx[v] = cache.c_off[lane]
            self.rem[v] = cache.c_len[lane]
            self.gain[v] = vel
            self.step[v] = 0.0

    def mix(self, cache):
        out = 0.0
        for v in range(len(self.rem)):
            if self.rem[v] <= 0:
                continue
            out += cache.data[self.idx[v]] * self.gain[v]
            self.idx[v] += 1
            self.rem[v] -= 1
            if self.step[v] != 0.0:
                self.gain[v] += self.step[v]
                if self.gain[v] <= 0.0:
                    self.rem[v] = 0
        return out * (1.0 / CACHE_ONE)


def render(beat_a, fill_a, bars_a, beat_b, fill_b, bars_b, bpm, level_n, bars, kit=0,
           mid_a=False, mid_b=False):
    # same order of Randm picks as drumbox.c: FillB at start, then each bar
    # (a fresh kit, as after init: the first downbeat has no crash)
    _, rng = resolve_fill(fill_a if bars_b == 0 else fill_b, FILL_RNG_SEED)
    last = 0
    since_crash = CRASH_GAP
    brng, rand_beat = BEAT_RNG_SEED, -1
    masks = masks_of(0, 0, 0)
    kp = KITS[kit][2]
    cache, players = KitCache(kp), Players()
    inc = bpm * 4.0 / (60.0 * SR)
    swing, sw_mask = 0.0, 1       # the bar's swing, set at each bar line (bar_swing)
    gain = level_n * 1.3
    vel_lo = 0.72                 # DB_VEL_LO: fixed accent depth
    step, phase = 15, 1.0
    sect, bar = 1, bars_b          # past section B's end: the first bar is section A's bar 0
    total = int(bars * 16 * SR / inc / SR) + int(SR)  # + 1 s tail
    out = []
    while not cache.ready & 4:              # start gate: silent until the closed hat is rendered
        cache.render_n()
        out.extend([0.0] * 8)
    count = [0] * len(PLANES)               # count-in bar: closed hat quarters, beat 1 accented
    count[SOUND_LANES[2]], count[ACCENT] = 0x1111, 1
    n_out, end = 0, 0
    counting = True                         # count-in bars until the kit is ready
    count_n = -1                            # countN: count-in bars played - 1
    while counting or n_out < end:
        if n_out % 8 == 0:                  # each audio call: render, then 8 samples
            cache.render_n()
        length = (1.0 - swing) if step & sw_mask else (1.0 + swing)
        phase += inc
        if phase >= length:
            phase -= length
            step = (step + 1) & 15
            if step == 0 and (cache.r_lane < len(SOUND_LANES) or (count_n > 1 and not count_n & 1)):
                masks = count               # count-in bar: kit still rendering, or 1 / even bars
                count_n += 1
            elif step == 0:
                if counting:                # the song starts: --bars from here
                    counting, end = False, n_out + total
                skip_a, skip_b = bars_a == 0, bars_b == 0      # Length Skip
                bar += 1
                if bar >= (bars_b if sect else bars_a):
                    if not (skip_a if sect else skip_b):
                        sect ^= 1
                    bar = 0
                if (skip_b if sect else skip_a) and not (skip_a if sect else skip_b):
                    sect ^= 1
                    bar = 0
                n = bars_b if sect else bars_a
                b = beat_b if sect else beat_a
                if n == 0:                  # both Skip: no drums, no fills, no crash
                    b = fill = last = 0
                else:
                    fill, rng = resolve_fill(section_fill(bar, n, fill_b if sect else fill_a,
                                                          mid_b if sect else mid_a), rng)
                if b == BEAT_RANDOM:        # same as drumbox.c: new beat per section start
                    if bar == 0 or rand_beat < 0:
                        rand_beat, brng = resolve_beat(brng)
                    b = rand_beat
                else:
                    rand_beat = -1
                # crash fill -> crash, unless the last one is < CRASH_GAP
                # bars back; a middle fill never crashes (last 0)
                since_crash += 1
                prev = last if since_crash >= CRASH_GAP else 0
                masks = masks_of(b, fill, prev)
                swing, sw_mask = bar_swing(b, fill, prev)
                if masks[5] & 1:
                    since_crash = 0
                last = fill if bar == n - 1 else 0
            players.trigger(cache, kp, masks, step, vel_lo)
        out.append(soft_clip(players.mix(cache)) * gain)
        n_out += 1
    return out


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(SR))
        w.writeframes(b"".join(
            struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))


def _pick(arg, names, lookup, default):
    """Knob value, screen name or 'all' -> list of indices."""
    if arg is None:
        return [lookup(default)]
    if arg == "all":
        return list(range(len(names)))
    if arg.isdigit():
        return [min(int(arg), len(names) - 1)]
    return [lookup(arg)]


def _length(arg):
    """Length knob value or name -> (bars, middle fill)."""
    _n, bars, mid = LENGTHS[min(int(arg), len(LENGTHS) - 1) if arg.isdigit() else length_index(arg)]
    return bars, mid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--beat-a", help=f"knob 0..{len(BEATS) - 1} or screen name, or 'all' "
                                    "(default: all beats)")
    ap.add_argument("--fill-a", help=f"knob 0..{len(FILLS) - 1} or screen name, or 'all' "
                                    f"(default {DEFAULT_FILL_A})")
    ap.add_argument("--beat-b", help=f"knob 0..{len(BEATS) - 1} or screen name, or 'all' "
                                    f"(default {DEFAULT_BEAT_B})")
    ap.add_argument("--fill-b", help=f"knob 0..{len(FILLS) - 1} or screen name, or 'all' "
                                    f"(default {DEFAULT_FILL_B})")
    lengths = ", ".join(n for n, _b, _m in LENGTHS)
    ap.add_argument("--len-a", default=DEFAULT_LEN_A,
                    help=f"LenA knob 0..{len(LENGTHS) - 1} or name ({lengths})")
    ap.add_argument("--len-b", default=DEFAULT_LEN_B,
                    help=f"LenB knob 0..{len(LENGTHS) - 1} or name ({lengths})")
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--level", type=float, default=0.7, help="Volume knob 0..1")
    ap.add_argument("--bars", type=int, default=8,
                    help="bars to render (default 8 = one 4Bar + 4Bar song)")
    ap.add_argument("--kit", default="0",
                    help="kit index, name or short name (%s), or 'all'"
                         % ", ".join(k[1] for k in KITS))
    ap.add_argument("--knob", type=int, help="Kit knob value 0..100 (overrides --kit)")
    a = ap.parse_args()

    out_dir = Path(__file__).resolve().parent / "preview"
    out_dir.mkdir(exist_ok=True)
    bas = _pick(a.beat_a or "all", BEATS, beat_index, None)
    fas = _pick(a.fill_a, FILLS, fill_index, DEFAULT_FILL_A)
    bbs = _pick(a.beat_b, BEATS, beat_index, DEFAULT_BEAT_B)
    fbs = _pick(a.fill_b, FILLS, fill_index, DEFAULT_FILL_B)
    (bars_a, mid_a), (bars_b, mid_b) = _length(a.len_a), _length(a.len_b)
    if a.knob is not None:
        kits = [knob_to_kit(a.knob)]
    elif a.kit == "all":
        kits = range(len(KITS))
    else:
        kits = [int(a.kit) if a.kit.isdigit() and int(a.kit) < len(KITS) else kit_index(a.kit)]
    for kit in kits:
        for ba in bas:
            for fa in fas:
                for bb in bbs:
                    for fb in fbs:
                        name = (f"{ba:02d}_{BEATS[ba][1]}_{FILLS[fa][1]}_"
                                f"{BEATS[bb][1]}_{FILLS[fb][1]}_{KITS[kit][1]}")
                        path = out_dir / f"{name}.wav"
                        s = render(ba, fa, bars_a, bb, fb, bars_b, a.bpm, a.level, a.bars, kit,
                                   mid_a, mid_b)
                        peak = max(abs(x) for x in s)
                        write_wav(path, s)
                        print(f"{path.name:44s} peak {peak:.2f}")


if __name__ == "__main__":
    main()
