#!/usr/bin/env python3
"""Analyse drum samples into DrumBox engine models (analysis-resynthesis).

For each WAV:
  1. partials: prominent spectral peaks are candidates; each is fitted as a
     gliding sine f(t) = f_end + f_delta * exp(-t / tau) by maximising the
     energy a demodulation along that chirp captures; its amplitude envelope
     is the demodulated magnitude. Kept if it captures enough energy, and
     subtracted from the signal (up to kits.MAX_PARTIALS).
  2. noise bands: the band energies of the sample (through the engine's own
     band-pass bank) at the envelope grid points, minus the partials' share,
     solved for band gains (non-negative, with the bank's overlap), then
     corrected by rendering the model and comparing, a few rounds.
  3. quantised to the engine's codes and written to drums/<prefix>_<drum>.json,
     one file per drum (an existing file keeps its "level").

Needs numpy (offline only; the build and preview.py don't).

Usage:
  python -B drum_analyze.py                       # all drums of the sample set
  python -B drum_analyze.py --only kick,snare     # some drums (others kept)
  python -B drum_analyze.py --report              # fit report of the saved models
  python -B drum_analyze.py --only ride1 --no-glide      # steady partials
  python -B drum_analyze.py --only ride1 --no-glide --min-tonality 0.05   # + noisy-band partials
  python -B drum_analyze.py --only ride1 --no-partials   # noise bands only

Cymbals and hats: fit them with --no-glide. Gliding partials come out as
falling chirps there (ride 2.7 kHz -> 459 Hz), heard as the pitch dropping;
noise only (--no-partials) sounded worse. The drum file
keeps "no_glide" / "no_partials": true, so a refit without the flag keeps it.

A new sample set: put its WAVs in samples/<set>/, add it to SETS below
(prefix, drum -> file), run with --set <set>, then use the new drums in
kits.py KIT_DEFS.
"""

from __future__ import annotations

import argparse
import json
import math
import wave
from pathlib import Path

import numpy as np

import kits as K

HERE = Path(__file__).resolve().parent
SR = K.SR
# sample set -> (drum file prefix, drum name -> sample file)
SETS = {
    "drum_kit_1": ("kit1", {"kick": "Kick.wav",
                            "tom_hi": "MedTom.wav", "tom_lo": "LowTom.wav",
                            "hh_cl": "HhC.wav", "hh_op": "HhO.wav",
                            "sn11": "sn11.wav",
                            }),
    "drum_kit_2": ("kit2", {"bd3": "bd 3.wav",
                            "kick1": "kick1.wav",
                            "kick_hvy": "heavy k1.wav",
                            "sd1": "sd 1.wav",
                            "chat": "hh cl.wav", "ohat": "hh op 1.wav",
                            "ride2": "ride2.wav",
                            "crash2": "crash2.wav",
                            "tom2": "tom2.wav", "tom3": "tom3.wav",
                            "sn23": "sn23.wav"}),
}
# drums whose file was renamed: <prefix>_<drum> -> file name in drums/
RENAMED = {"kit2_ride2": "ride", "kit2_crash2": "crash", "kit1_sn11": "snare1",
           "kit2_chat": "hh_cl2", "kit2_ohat": "hh_op2",
           "kit1_tom_hi": "tom_hi1", "kit1_tom_lo": "tom_lo1", "kit1_hh_cl": "hh_cl1", "kit1_hh_op": "hh_op1",
           "kit1_kick": "kick1", "kit2_kick1": "kick2",
           "kit2_sd1": "snare2", "kit2_tom2": "tom_hi2", "kit2_tom3": "tom_lo2",
           "kit2_sn23": "snare3", "kit2_bd3": "kick3",
           "kit2_kick_hvy": "kick4"}
SET = "drum_kit_1"            # --set
SAMPLES = HERE / "samples" / SET
PREFIX, SOURCES = SETS[SET]

B0 = np.array([b[0] for b in K.BANDS])
A1 = np.array([b[1] for b in K.BANDS])
A2 = np.array([b[2] for b in K.BANDS])
GRID = np.array(K.GRID)
NB = K.NUM_BANDS


def load(name: str) -> np.ndarray:
    with wave.open(str(SAMPLES / name)) as w:
        ch, n, sw = w.getnchannels(), w.getnframes(), w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        raise ValueError(f"{name}: need 16-bit PCM")
    a = np.frombuffer(raw, dtype="<i2").astype(float).reshape(-1, ch).mean(axis=1) / 32768.0
    on = int(np.argmax(np.abs(a) > 0.05 * np.abs(a).max()))
    return a[max(0, on - 8):]


# --- the engine's noise and band bank ------------------------------------------

def xorshift(seed: int, n: int) -> np.ndarray:
    out = np.empty(n)
    x = seed
    for i in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = (x - 0x100000000 if x >= 0x80000000 else x) * 4.6566129e-10
    return out


def bank(x: np.ndarray, two_inputs: np.ndarray | None = None) -> np.ndarray:
    """Band-pass bank, (n, NB). With two_inputs (2, n): even bands filter
    row 0, odd bands row 1 (the engine's two noise sources)."""
    n = (two_inputs if x is None else x).shape[-1]
    src = (np.stack([two_inputs[b & 1] for b in range(NB)]) if two_inputs is not None
           else np.broadcast_to(x, (NB, n)))
    d = src.copy()
    d[:, 2:] -= src[:, :-2]
    out = np.empty((n, NB))
    y1 = np.zeros(NB)
    y2 = np.zeros(NB)
    dT = d.T
    for i in range(n):
        y = B0 * dT[i] - A1 * y1 - A2 * y2
        y2 = y1
        y1 = y
        out[i] = y
    return out


_NOISE = None


def noise_bank(n: int) -> np.ndarray:
    """Engine noise through the bank, unit band gains (cached)."""
    global _NOISE
    if _NOISE is None or len(_NOISE) < n:
        m = max(n, int(1.8 * SR))
        _NOISE = bank(None, np.stack([xorshift(0x1234567, m), xorshift(0x2C9277B5, m)]))
    return _NOISE[:n]


MIN_WIN = np.array([2.0 * SR / f for f in K.BAND_HZ])    # 2 periods per band


def grid_power(y: np.ndarray) -> np.ndarray:
    """Mean power per band around each grid point 1..NUM_SEG, (NUM_SEG, NB);
    each band's window spans at least 2 periods of its centre frequency."""
    n = len(y)
    c = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y ** 2, axis=0)])
    out = np.zeros((K.NUM_SEG, y.shape[1]))
    for k in range(1, K.NUM_SEG + 1):
        a = (GRID[k - 1] + GRID[k]) // 2
        b = (GRID[k] + GRID[k + 1]) // 2 if k < K.NUM_SEG else GRID[k]
        half = np.maximum((b - a) / 2.0, MIN_WIN / 2.0)
        mid = (a + b) / 2.0
        lo = np.maximum((mid - half).astype(int), 0)
        hi = (mid + half).astype(int)
        span = hi - lo                       # past the end of y counts as silence
        cols = np.arange(y.shape[1])
        out[k - 1] = (c[np.minimum(hi, n), cols] - c[np.minimum(lo, n), cols]) / np.maximum(span, 1)
    return out


def env_curve(vals: np.ndarray, n: int) -> np.ndarray:
    """Engine envelope: 0 at t = 0, linear to vals[k] at GRID[k + 1]."""
    return np.interp(np.arange(n), GRID, np.concatenate([[0.0], vals]), right=0.0)


def db_sin(p: np.ndarray) -> np.ndarray:
    x = p * 2.0 - 1.0
    y = 4.0 * x * (1.0 - np.abs(x))
    return y + 0.225 * (y * np.abs(y) - y)


def chirp_phase(f_end: float, f_delta: float, tau: float, n: int) -> np.ndarray:
    """Phase in cycles, as the engine: pd *= glide, then ph += (f_end + pd) / SR."""
    glide = math.exp(-1.0 / (tau * SR)) if tau > 0 else 0.0
    f = f_end + f_delta * glide ** np.arange(1, n + 1)
    return np.cumsum(f) / SR


def render_partials(parts: list[dict], scale: float, n: int) -> np.ndarray:
    out = np.zeros(n)
    for p in parts:
        tau = -1.0 / (SR * math.log(p["glide"])) if 0 < p["glide"] < 1 else 0.0
        ph = chirp_phase(p["f_end"], p["f_delta"], tau, n)
        amp = env_curve(np.array([K.amp_of_code(q) for q in p["env"]]) * scale, n)
        out += amp * db_sin(ph % 1.0)
    return out


def render_model(m: dict, n: int) -> np.ndarray:
    """One hit of a model (no velocity scaling), as the engine plays it."""
    y = render_partials(m["partials"], m["part_scale"], n)
    nb = noise_bank(n)
    for i, env in enumerate(m["band_env"]):
        b = m["band_lo"] + i
        y += env_curve(np.array([K.amp_of_code(q) for q in env]) * m["band_scale"], n) * nb[:, b]
    return y


# --- partials -----------------------------------------------------------------

def candidates(x: np.ndarray, max_n: int = 10) -> list[float]:
    """Frequencies of prominent spectral peaks: the strongest peaks of a few
    time windows first (a gliding partial settles late), then of the whole
    hit, strongest first, at least a semitone apart."""
    out: list[float] = []
    n = len(x)
    for a, b in ((0.03, 0.12), (0.012, 0.04), (0.1, 0.4)):
        i0, i1 = int(a * SR), min(n, int(b * SR))
        if i1 - i0 < 256:
            continue
        for f in _peaks(x[i0:i1], 2, -99.0):
            if all(abs(math.log2(f / g)) > 1 / 12 for g in out):
                out.append(f)
    for f in _peaks(x[:min(n, int(0.6 * SR))], max_n, 6.0):
        if len(out) >= max_n:
            break
        if all(abs(math.log2(f / g)) > 1 / 12 for g in out):
            out.append(f)
    return out


def _peaks(seg: np.ndarray, max_n: int, min_prom: float) -> list[float]:
    N = 1 << 17
    p = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N)) ** 2
    f = np.fft.rfftfreq(N, 1.0 / SR)
    lp = np.log(p + 1e-30)
    # smooth over +-1/6 octave (log-frequency moving average)
    smooth = np.empty_like(lp)
    c = np.concatenate([[0.0], np.cumsum(lp)])
    lo = np.searchsorted(f, f / 2 ** (1 / 6))
    hi = np.searchsorted(f, f * 2 ** (1 / 6))
    hi = np.maximum(hi, lo + 1)
    smooth = (c[hi] - c[lo]) / (hi - lo)
    prom = (lp - smooth) * 10 / math.log(10)
    ok = (f > 35) & (f < 14000)
    pk = np.nonzero(ok[1:-1] & (p[1:-1] > p[:-2]) & (p[1:-1] >= p[2:]) & (prom[1:-1] > min_prom))[0] + 1
    pk = pk[np.argsort(-p[pk])]
    out: list[float] = []
    for i in pk:
        if all(abs(math.log2(f[i] / g)) > 1 / 12 for g in out):
            out.append(float(f[i]))
        if len(out) >= max_n:
            break
    return out


def demod(x: np.ndarray, ph: np.ndarray, f_end: float, periods: float = 1.0) -> np.ndarray:
    """Complex amplitude of x along phase ph (boxcar low-pass of `periods`
    periods of f_end, at least 2 ms): component = Re(a * exp(2 pi i ph))."""
    L = max(int(round(SR / f_end * periods)), int(0.002 * SR))
    z = x * np.exp(-2j * np.pi * ph)
    c = np.concatenate([[0], np.cumsum(z)])
    n = len(x)
    i = np.arange(n)
    a = np.clip(i - L // 2, 0, n)
    b = np.clip(i - L // 2 + L, 0, n)
    return 2.0 * (c[b] - c[a]) / np.maximum(b - a, 1)


def fit_partial(x: np.ndarray, f0: float, glide: bool = True) -> tuple[float, float, float, np.ndarray, float]:
    """Best chirp near f0: (f_end, f_delta, tau, component, captured energy);
    glide False: a steady sine (f_delta 0).
    Score: energy removed when the component (demodulated over 2 periods, so
    a wrong glide is penalised) is subtracted."""
    n = len(x)
    best = None
    ex = float(np.sum(x ** 2))

    def score(fe, fd, tau):
        ph = chirp_phase(fe, fd, tau, n)
        c = np.real(demod(x, ph, fe, 2.0) * np.exp(2j * np.pi * ph))
        return ex - float(np.sum((x - c) ** 2)), None

    for fe in f0 * np.array([0.85, 0.92, 1.0, 1.08, 1.15]):
        for r in ((0.0, 0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 6.0) if glide else (0.0,)):
            for tau in ((0.0,) if r == 0 else (0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2)):
                s, _a = score(fe, fe * r, tau)
                if best is None or s > best[0]:
                    best = (s, fe, fe * r, tau)
    s, fe, fd, tau = best
    step = {"fe": 0.02, "fd": 0.2, "tau": 0.3}
    for _round in range(4):
        improved = True
        while improved:
            improved = False
            for k in (("fe", "fd", "tau") if glide else ("fe",)):
                for sgn in (1, -1):
                    cfe, cfd, ctau = fe, fd, tau
                    if k == "fe":
                        cfe = fe * (1 + sgn * step["fe"])
                    elif k == "fd":
                        cfd = max(0.0, fd + sgn * step["fd"] * fe)
                    else:
                        if tau <= 0:
                            continue
                        ctau = tau * math.exp(sgn * step["tau"])
                    cs, _a = score(cfe, cfd, ctau)
                    if cs > s * (1 + 1e-6):
                        s, fe, fd, tau = cs, cfe, cfd, ctau
                        improved = True
        for k in step:
            step[k] *= 0.5
    if fd < 0.01 * fe:
        fd, tau = 0.0, 0.0
    ph = chirp_phase(fe, fd, tau, n)
    a = demod(x, ph, fe)
    comp = np.real(a * np.exp(2j * np.pi * ph))
    return fe, fd, tau, comp, float(np.sum(comp ** 2))


def tonality(x: np.ndarray, fe: float, fd: float, tau: float) -> float:
    """Energy along the chirp through an 8 ms window / through a 2 ms one:
    near 1 for a sine (its energy is all in the narrow band), ~0.25 for
    noise (energy grows with the bandwidth)."""
    n = len(x)
    ph = chirp_phase(fe, fd, tau, n)
    wide = np.sum(np.abs(demod(x, ph, 1e9)) ** 2)            # 2 ms (the minimum)
    L8 = 0.008 * SR * fe / SR                                  # 8 ms in periods of fe
    narrow = np.sum(np.abs(demod(x, ph, fe, L8)) ** 2)
    return float(narrow / max(wide, 1e-30))


def nnls(A: np.ndarray, b: np.ndarray, iters: int = 200) -> np.ndarray:
    """min |A x - b|, x >= 0 (Lawson-Hanson active set)."""
    m = A.shape[1]
    x = np.zeros(m)
    free = np.zeros(m, bool)
    for _ in range(iters):
        g = A.T @ (b - A @ x)
        cand = np.where(~free & (g > 1e-12 * max(1.0, np.abs(g).max())))[0]
        if not len(cand):
            break
        free[cand[np.argmax(g[cand])]] = True
        while True:
            z = np.zeros(m)
            z[free] = np.linalg.lstsq(A[:, free], b, rcond=None)[0]
            if np.all(z[free] > 0):
                x = z
                break
            neg = free & (z <= 0)
            alpha = np.min(x[neg] / np.maximum(x[neg] - z[neg], 1e-30))
            x = x + alpha * (z - x)
            free &= x > 1e-12
            x[~free] = 0.0
    return x


def fit_env(a: np.ndarray) -> np.ndarray:
    """Grid values whose engine envelope (linear between points, 0 at t = 0
    and at the last point) best follows the curve a, weighted towards
    relative error (quiet tails count); non-negative (solved as such, not
    clipped afterwards: clipping let a point swing up again after a zero).
    The sample's end is silence, so no point past it can bring a tone back."""
    n = len(a)
    m = K.NUM_SEG - 1                       # free values (the last one is 0)
    t = np.arange(GRID[-1])
    a = np.concatenate([a, np.zeros(max(0, len(t) - n))])[:len(t)]
    basis = np.empty((len(t), m))
    for k in range(m):
        e = np.zeros(K.NUM_SEG + 1)
        e[k + 1] = 1.0
        basis[:, k] = np.interp(t, GRID, e)
    w = 1.0 / (a + 0.01 * a.max())
    v = nnls(basis * w[:, None], a * w)
    return np.concatenate([v, [0.0]])


def amp_points(env: np.ndarray) -> np.ndarray:
    """Mean amplitude around each grid point 1..NUM_SEG (last one 0)."""
    n = len(env)
    out = np.zeros(K.NUM_SEG)
    for k in range(1, K.NUM_SEG):
        a = min((GRID[k - 1] + GRID[k]) // 2, n)
        b = min((GRID[k] + GRID[k + 1]) // 2, n)
        if b - a >= 2:
            out[k - 1] = np.mean(env[a:b])
    return out


# --- whole drum ---------------------------------------------------------------

def nonneg_solve(A: np.ndarray, t: np.ndarray, iters: int = 300) -> np.ndarray:
    """min |A g - t|, g >= 0 (multiplicative updates)."""
    g = np.maximum(t / np.maximum(np.diag(A), 1e-30), 1e-30)
    AtA = A.T @ A
    At_t = A.T @ t
    for _ in range(iters):
        g *= At_t / np.maximum(AtA @ g, 1e-30)
    return g


def quantise(vals: np.ndarray) -> tuple[float, list[list[int]]]:
    scale = float(vals.max()) if vals.max() > 0 else 1.0
    return scale, [[K.code_of_amp(v / scale) for v in row] for row in vals]


def fit_report(x: np.ndarray, m: dict) -> dict:
    n = len(x)
    y = render_model(m, n)
    pt = grid_power(bank(x))
    pm = grid_power(bank(y))
    keep = pt > pt.max() * 10 ** (-5.0)
    err = 10 * np.log10((pm[keep] + 1e-20) / pt[keep])
    h = int(0.005 * SR)
    ex = np.array([np.mean(x[i:i + h] ** 2) for i in range(0, n - h, h)])
    ey = np.array([np.mean(y[i:i + h] ** 2) for i in range(0, n - h, h)])
    ok = ex > ex.max() * 1e-5
    eerr = 10 * np.log10((ey[ok] + 1e-20) / ex[ok])
    return {"band_rms_db": round(float(np.sqrt(np.mean(err ** 2))), 2),
            "env_rms_db": round(float(np.sqrt(np.mean(eerr ** 2))), 2),
            "old_loss": round(old_metric(x, y), 1)}


def old_metric(x: np.ndarray, y: np.ndarray) -> float:
    """The loss sample_fit.py used for the parameter fits (comparison only)."""
    W = ((0, 5), (5, 15), (15, 40), (40, 80), (80, 150), (150, 300), (300, 600), (600, 1200))
    E = (30, 60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000, 22050)

    def feats(s):
        cells = []
        for a, b in W:
            i0, i1 = int(a * SR / 1000), min(int(b * SR / 1000), len(s))
            if i1 - i0 < 64:
                break
            p = np.abs(np.fft.rfft(s[i0:i1] * np.hanning(i1 - i0), 8192)) ** 2
            f = np.fft.rfftfreq(8192, 1.0 / SR)
            cells += [10 * math.log10(p[(f >= lo) & (f < hi)].sum() / (i1 - i0) + 1e-20)
                      for lo, hi in zip(E, E[1:])]
        cells = np.array(cells)
        h = int(0.005 * SR)
        env = np.array([10 * math.log10(np.mean(s[i:i + h] ** 2) + 1e-20)
                        for i in range(0, len(s) - h, h)])
        return np.maximum(cells - cells.max(), -50), np.maximum(env - env.max(), -50)
    cx, ex = feats(x)
    cy, ey = feats(y)
    m = min(len(ex), len(ey))
    return float(np.mean((cy - cx) ** 2) + 0.5 * np.mean((ey[:m] - ex[:m]) ** 2))


def analyse(name: str, wav: str, verbose: bool = True, max_partials: int = K.MAX_PARTIALS,
            glide: bool = True, min_tonality: float = 0.4) -> dict:
    x = load(wav)
    n = len(x)
    total = float(np.sum(x ** 2))
    # 1. partials, greedy
    resid = x.copy()
    parts, comps = [], []
    for f0 in candidates(x):
        if len(parts) >= max_partials:
            break
        fe, fd, tau, comp, e = fit_partial(resid, f0, glide)
        frac = e / total
        tonal = tonality(resid, fe, fd, tau)
        if verbose:
            print(f"  cand {f0:7.1f} Hz -> f_end {fe:7.1f}, delta {fd:7.1f}, tau {tau * 1000:6.1f} ms, "
                  f"captures {frac * 100:5.1f} %, tonality {tonal:.2f}")
        if frac < 0.02 or tonal < min_tonality:
            continue
        # amplitude through half a period (still nulls the 2f image): sharp edges
        parts.append((fe, fd, tau, np.abs(demod(resid, chirp_phase(fe, fd, tau, n), fe, 0.5))))
        comps.append(comp)
        resid = resid - comp
    pvals = np.array([fit_env(p[3]) for p in parts]) if parts else np.zeros((0, K.NUM_SEG))
    part_scale, part_codes = quantise(pvals) if parts else (1.0, [])
    model = {"src": wav, "partials": [
        {"f_end": round(fe, 3), "f_delta": round(fd, 3),
         "glide": (math.exp(-1.0 / (tau * SR)) if tau > 0 else 0.0), "env": codes}
        for (fe, fd, tau, _e), codes in zip(parts, part_codes)], "part_scale": part_scale}
    tonal = render_partials(model["partials"], part_scale, n)
    # 2. noise bands
    target = grid_power(bank(x))                       # (seg, band)
    p_tonal = grid_power(bank(tonal))
    # noise fills what the partials leave; where they carry most of a band's
    # energy the band is theirs (noise there would only patch their envelope)
    need = np.where(p_tonal > 0.2 * target, 0.0, np.maximum(target - p_tonal, 0.0))
    # 3-point median over time per band: no isolated one-point bursts
    padded = np.concatenate([need[:1], need, need[-1:]])
    need = np.median(np.stack([padded[:-2], padded[1:-1], padded[2:]]), axis=0)
    nb = noise_bank(n)
    # A[k][b][c]: band-b power of unit-gain band c noise (time-invariant)
    seg_len = int(0.3 * SR)
    long_nb = noise_bank(seg_len)
    A = np.array([[np.mean(bank(long_nb[:seg_len, c])[seg_len // 3:, b] ** 2) for c in range(NB)]
                  for b in range(NB)])
    # start: each band's need shared out over the bands that leak into it
    g = np.sqrt(need / A.sum(axis=1))
    g[-1] = 0.0
    for _round in range(8):
        y = np.zeros(n)
        for b in range(NB):
            y += env_curve(g[:, b], n) * nb[:, b]
        p_noise = grid_power(bank(y))
        ratio = np.sqrt(np.clip((need + 1e-24) / (p_noise + 1e-24), 0.33, 3.0))
        g = g * np.where(need > target.max() * 1e-7, ratio, 0.0)
        g[-1] = 0.0
    # 3. quantise, drop bands that stay quiet
    gmax = g.max(axis=0)
    used = np.nonzero(gmax > g.max() * 10 ** (-45 / 20))[0] if g.max() > 0 else np.array([0])
    lo, hi = int(used.min()), int(used.max())
    band_scale, band_codes = quantise(g[:, lo:hi + 1].T)
    model.update(band_lo=lo, band_hi=hi, band_scale=band_scale, band_env=band_codes)
    model["fit"] = fit_report(x, model)
    return model


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="comma-separated drum names")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--set", default=SET, choices=sorted(SETS), help="sample set")
    ap.add_argument("--no-partials", action="store_true",
                    help="noise bands only; kept in the drum file")
    ap.add_argument("--no-glide", action="store_true",
                    help="steady partials, no pitch glide (cymbals, hats); kept in the drum file")
    ap.add_argument("--min-tonality", type=float,
                    help="partial tonality threshold (default 0.4; cymbals 0.05); kept in the drum file")
    a = ap.parse_args()
    global SAMPLES, PREFIX, SOURCES
    SAMPLES = HERE / "samples" / a.set
    PREFIX, SOURCES = SETS[a.set]
    names = a.only.split(",") if a.only else list(SOURCES)

    def path_of(d):
        did = f"{PREFIX}_{d}"
        return K.DRUM_DIR / f"{RENAMED.get(did, did)}.json"

    if a.report:
        for d in names:
            if not path_of(d).exists():     # not in any kit, file removed
                print(f"{d:7s} no {path_of(d).name} in drums/")
                continue
            print(f"{d:7s} {fit_report(load(SOURCES[d]), json.loads(path_of(d).read_text()))}")
        return
    for d in names:
        print(f"{d}: {SOURCES[d]}")
        path = path_of(d)
        old = json.loads(path.read_text()) if path.exists() else {}
        no_parts = a.no_partials or old.get("no_partials", False)
        no_glide = a.no_glide or old.get("no_glide", False)
        min_ton = a.min_tonality if a.min_tonality is not None else old.get("min_tonality")
        m = analyse(d, SOURCES[d], max_partials=0 if no_parts else K.MAX_PARTIALS, glide=not no_glide,
                    min_tonality=0.4 if min_ton is None else min_ton)
        print(f"  -> {len(m['partials'])} partials, bands {K.BAND_HZ[m['band_lo']]}..{K.BAND_HZ[m['band_hi']]} Hz, "
              f"fit {m['fit']}")
        # level: the mix level (kits.py multiplies the scales by it); a new
        # drum starts at the 0.5 mix trim, set it by ear / loudness match
        out = {"engine": "resynth", "set": a.set, "level": old.get("level", 0.5)}
        if "band_cap_db" in old:        # hand-set attack cap (kits.load_drums)
            out["band_cap_db"] = old["band_cap_db"]
        if no_parts:
            out["no_partials"] = True
        if no_glide:
            out["no_glide"] = True
        if min_ton is not None:
            out["min_tonality"] = min_ton
        out.update(m)
        path.write_text(K.dump_drum(out))
        print(f"  wrote {path.name}")


if __name__ == "__main__":
    main()
