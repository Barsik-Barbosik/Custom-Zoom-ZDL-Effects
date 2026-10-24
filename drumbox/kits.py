#!/usr/bin/env python3
"""DrumBox drum palette and kits: sampled (resynthesis) and synth drums.

A kit maps the eight pattern lanes (kick, snare, closed hat, open hat, ride,
crash, high tom, low tom) to drums from the palette in drums/, one file per
drum; a kit can mix both kinds. A closed hat chokes the drum on the
open-hat lane. Only drums used by a kit go into the ZDL.

Sampled drums, drums/<id>.json (drum_analyze.py output, + "level"): data for
one generic engine (drumbox.c db_render, mirrored by preview.py Voice):

  partials  up to MAX_PARTIALS sines, each with a pitch glide
            f(t) = f_end + f_delta * glide^t (glide = per-sample multiplier)
            and an amplitude envelope (one code per GRID point)
  bands     noise through a fixed bank of NUM_BANDS third-octave band-pass
            filters (BAND_HZ, shared by all drums); each drum has an
            amplitude envelope per band (bands band_lo..band_hi only)

Envelopes: one amplitude code (0..127) per GRID point 1..NUM_SEG, linear in
amplitude between points; a hit starts from the drum's current amplitude
(no click on a retrigger) and the drum stops after the last point (its code
is 0). Code q -> amplitude scale * 2^(-(127 - q) / 12) (0.5 dB steps, 63 dB
range), 0 = silence; amp_of_code() / db_amp_code() in drumbox.c. Rows are
stored compressed (encode_row: trimmed at both ends + 4-bit steps, ~0.8 dB
mean error);
everything in Python plays the decoded rows, as the DSP does. "level"
scales the model.

Noise: two xorshift32 generators, even bands from one, odd bands from the
other, so neighbouring bands are independent (no phase cancellation where
their skirts overlap). Band filter: RBJ constant-peak band-pass,
y = b0 * (x[n] - x[n-2]) - a1 * y[n-1] - a2 * y[n-2].

Synth drums, each with its own code: drums/<id>.c (C, compiled into the
ZDL) + drums/<id>.py (PARAMS, LEVEL, STATE and the Python mirror hit() /
render(), line for line). C contract: a state struct of at most
sum(STATE) words in DBSyn.s (DB_SYN_FITS checks it),
db_syn_<id>_hit(DBSyn *v, const float *p, float vel) and
float db_syn_<id>_render(DBSyn *v, const float *p), which clears v->active
when the drum is done; params as p[P_<ID>_<NAME>], noise from db_noise(v).
A drum file with only a .py and ALGO = "<id>" reuses that code with its own
PARAMS overrides. The output is times LEVEL; the lane's choke fade is
applied by drumbox.c.

Kit knob: one knob step per kit (build.py sets the max to len(KITS) - 1).

Usage: python -B kits.py   # regenerate drumbox_kits.h + drumbox_synth.h
"""

from __future__ import annotations

import importlib.util
import json
import os
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
SR = 44100.0

# Envelope time grid (ms): dense at the attack, sparse in the tail.
GRID_MS = (0, 1, 2, 3, 5, 7, 10, 14, 19, 25, 33, 43, 56, 72, 93, 120, 155, 200, 260,
           340, 440, 570, 740, 960, 1250, 1700)
GRID = [int(round(ms * SR / 1000.0)) for ms in GRID_MS]
NUM_SEG = len(GRID) - 1              # envelope codes per partial / band
GRID_INV = [1.0 / (GRID[k + 1] - GRID[k]) for k in range(NUM_SEG)]
CHOKE_SAMPLES = 132                  # a choked open hat fades out in 3 ms

# Third-octave noise bands (ISO centres), constant-peak band-pass, Q 4.32.
BAND_HZ = (63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
           1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000)
NUM_BANDS = len(BAND_HZ)
BAND_Q = 4.32
MAX_PARTIALS = 4


def band_coefs(f: float, q: float = BAND_Q) -> tuple[float, float, float]:
    w = 2.0 * math.pi * f / SR
    alpha = math.sin(w) / (2.0 * q)
    return (alpha / (1.0 + alpha), -2.0 * math.cos(w) / (1.0 + alpha),
            (1.0 - alpha) / (1.0 + alpha))


BANDS = [band_coefs(f) for f in BAND_HZ]          # (b0, a1, a2) per band
FRAC12 = [2.0 ** (-j / 12.0) for j in range(12)]


def amp_of_code(q: int) -> float:
    """Same as db_amp_code() in drumbox.c (integer split, no pow)."""
    if q <= 0:
        return 0.0
    e = 127 - q
    k = (e * 2731) >> 15              # e // 12 for e in 0..127
    return FRAC12[e - 12 * k] * 2.0 ** (-k)


# Compressed envelope rows (what the DSP stores): L = the number of points
# up to the last non-zero one (L 0 = silent row, the byte 0 alone), S = the
# first non-zero point rounded down to even (leading silence, at most
# ROW_S_MAX); byte 0 = L | S / 2 << 5, byte 1 = code of point S, then
# one 4-bit step per point after S, two per byte (low nibble first):
# code[j] = clamp(code[j - 1] + ENV_STEPS[nibble], 0, 127); points before
# S and from L on are 0. The encoder picks the steps by dynamic
# programming (least squared error over the row, points more than 30 dB
# under the row's peak weighted 1/20), not greedily.
ENV_STEPS = (-96, -56, -32, -18, -10, -5, -2, 0, 2, 5, 10, 18, 32, 56, 96, 127)
ROW_S_MAX = 14                        # S / 2 in byte 0's top 3 bits


def encode_row(env: list[int]) -> list[int]:
    L = max((i + 1 for i, q in enumerate(env) if q), default=0)
    if not L:
        return [0]
    S = min(next(i for i, q in enumerate(env) if q), ROW_S_MAX) & ~1
    top = max(env)
    w = [1.0 if q >= top - 60 else 0.05 for q in env]
    cost = {c: w[S] * (env[S] - c) ** 2 for c in range(128)}   # any start code
    back = []
    for i in range(S + 1, L):
        nc: dict[int, float] = {}
        bk: dict[int, tuple[int, int]] = {}
        for c, v in cost.items():
            for j, d in enumerate(ENV_STEPS):
                n = max(0, min(127, c + d))
                e = v + w[i] * (env[i] - n) ** 2
                if e < nc.get(n, 1e30):
                    nc[n] = e
                    bk[n] = (c, j)
        cost = nc
        back.append(bk)
    c = min(cost, key=cost.get)
    nibs = []
    for bk in reversed(back):
        c, j = bk[c]
        nibs.append(j)
    nibs.reverse()
    out = [L | (S >> 1) << 5, c]
    for i in range(0, len(nibs), 2):
        out.append(nibs[i] | ((nibs[i + 1] << 4) if i + 1 < len(nibs) else 0))
    return out


def decode_row(row: list[int], n: int = None) -> list[int]:
    """Same as db_code_next() in drumbox.c, all points."""
    n = NUM_SEG if n is None else n
    L = row[0] & 31
    codes = [0] * n
    if L:
        S = (row[0] >> 4) & 14
        codes[S] = row[1]
        for j in range(S + 1, L):
            k = j - S - 1
            b = row[2 + (k >> 1)]
            nib = (b >> 4) if k & 1 else (b & 15)
            codes[j] = max(0, min(127, codes[j - 1] + ENV_STEPS[nib]))
    return codes


def code_of_amp(a: float) -> int:
    """Nearest code for amplitude a (relative to the drum's scale, <= 1)."""
    if a <= 2.0 ** (-126 / 12.0) * 0.5:
        return 0
    return max(1, min(127, int(round(127 + 12.0 * math.log2(a)))))


# --- drum palette (drums/) ----------------------------------------------------

DRUM_DIR = HERE / "drums"
LANES = ("kick", "snare", "chat", "ohat", "ride", "crash", "htom", "ltom")


def _load_module(path: Path):
    spec = importlib.util.spec_from_file_location(f"drum_{path.stem}", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# Real-sample starts (drum_sample.py): the drum's "sample"
# field, decoded by the DSP while the drum plays. The sample has nothing
# above ~6.6 kHz (14,700 Hz rate): bands from SMP_HI_HZ up play under it.
SMP_DIV = 3
SMP_HI_HZ = 7277
SMP_MULT_WORDS = 4
SMP_STEP_MIN, SMP_STEP_MAX = 1e-5, 0.5
SMP_MULT = {2: (0.8, 1.6), 3: (0.9, 0.9, 1.25, 1.75)}   # step multiplier per magnitude


def _load_sample(m: dict) -> None:
    """Zero the body's band rows under SMP_HI_HZ and its partials up to the
    crossfade start (the grid point xf_ms[0]): they fade in over the
    segment to xf_ms[1] while the sample fades out. m["smp"] = what the
    DSP gets."""
    s = m["sample"]
    if s["div"] != SMP_DIV:
        raise ValueError(f"drum {m['id']}: sample div must be {SMP_DIV}")
    xa, xb = s["xf_ms"]
    ka = GRID_MS.index(xa) - 1          # band_env[k] is the code at GRID[k + 1]
    for i, r in enumerate(m["band_env"]):
        if BAND_HZ[m["band_lo"] + i] < SMP_HI_HZ:
            r[:ka + 1] = [0] * (ka + 1)
    for p in m["partials"]:
        p["env"][:ka + 1] = [0] * (ka + 1)
    fa, fb = GRID[ka + 1], GRID[GRID_MS.index(xb)]
    mult = list(SMP_MULT[s["bits"]])
    m["smp"] = {"data": bytes.fromhex(s["data"]), "bits": s["bits"], "a": s["a"],
                "step0": s["step0"], "gain": s["gain"], "fa": fa, "fb": fb,
                "inv": 1.0 / (fb - fa), "n": s["n"],
                "mult": mult + [1.0] * (SMP_MULT_WORDS - len(mult))}
    if (fb + SMP_DIV - 1) // SMP_DIV + 2 > s["n"]:
        raise ValueError(f"drum {m['id']}: sample shorter than its fade end")


def load_resynth(m: dict, did: str) -> dict:
    """A sampled drum's json data -> the drum as the DSP gets it."""
    if m.get("engine") != "resynth":
        raise ValueError(f"{did}.json: engine must be 'resynth'")
    m["id"] = did
    # Optional "band_cap_db": no band point louder than this many dB
    # below the drum's loudest one (code 127). The fit can fill a
    # 1-2 ms attack with a loud noise burst whose random peaks are far
    # above the sample's and clip (kick1: 1 kHz band at 0 dB, peak 0.56 vs
    # a body rms of 0.07; capped at -24 dB the attack/body energy matches
    # the WAV's).
    if "band_cap_db" in m:
        lim = 127 + int(round(2 * m["band_cap_db"]))
        m["band_env"] = [[min(c, lim) for c in r] for r in m["band_env"]]
    if "sample" in m:
        _load_sample(m)
    # Compress every envelope row as the DSP stores it and play the
    # decoded codes everywhere (preview.py, reports), so Python hears
    # what the pedal does.
    m["enc_band"] = [encode_row(r) for r in m["band_env"]]
    m["band_env"] = [decode_row(e) for e in m["enc_band"]]
    m["enc_part"] = [encode_row(p["env"]) for p in m["partials"]]
    for p, e in zip(m["partials"], m["enc_part"]):
        p["env"] = decode_row(e)
    m["band_scale"] *= m["level"]
    m["part_scale"] *= m["level"]
    return m


def dump_drum(m: dict) -> str:
    """A sampled drum's json text, as every drum file is written:
    indent 1, but each "env" row and each "band_env" row on
    one line ([12, 9, 0]), so an envelope reads as one row of codes."""
    rows = []

    def mark(r: list) -> str:
        rows.append(json.dumps(r))
        return f"@@row{len(rows) - 1}@@"

    m = dict(m)
    if "band_env" in m:
        m["band_env"] = [mark(r) for r in m["band_env"]]
    if "partials" in m:
        m["partials"] = [dict(q, env=mark(q["env"])) if "env" in q else q for q in m["partials"]]
    text = json.dumps(m, indent=1)
    for i, r in enumerate(rows):
        text = text.replace(json.dumps(f"@@row{i}@@"), r)
    return text


def load_drums() -> dict[str, dict]:
    """Every drum file in drums/ -> {id: drum}. Sampled drums (*.json) get
    their envelope rows compressed and decoded as the DSP stores them and
    their scales times their level; synth drums (*.py) get their algorithm
    module (their own, or ALGO's) and its PARAMS with their overrides."""
    import sys
    if str(HERE) not in sys.path:
        sys.path.insert(0, str(HERE))       # drum files import synth_lib
    drums: dict[str, dict] = {}
    for path in sorted(DRUM_DIR.glob("*.json")):
        drums[path.stem] = load_resynth(json.loads(path.read_text()), path.stem)
    mods = {path.stem: _load_module(path) for path in sorted(DRUM_DIR.glob("*.py"))}
    for did, mod in mods.items():
        if did in drums:
            raise ValueError(f"drum {did!r} is both a .json and a .py file")
        algo = getattr(mod, "ALGO", did)
        if algo not in mods or hasattr(mods[algo], "ALGO"):
            raise ValueError(f"drum {did}: ALGO {algo!r} is not a drum file with its own code")
        code = mods[algo]
        if not (DRUM_DIR / f"{algo}.c").exists():
            raise ValueError(f"drum {did}: no drums/{algo}.c")
        extra = set(mod.PARAMS) - set(code.PARAMS)
        if extra:
            raise ValueError(f"drum {did}: {sorted(extra)} are not params of {algo}")
        drums[did] = {"id": did, "engine": "synth", "algo": algo, "code": code,
                      "params": {**code.PARAMS, **mod.PARAMS}, "level": float(mod.LEVEL)}
    return drums


DRUMS = load_drums()

# Kits: (name, screen name <= 5 chars, drum id per lane in LANES order).
# Any drum on any lane, sampled and synth drums mixed freely; a lane plays
# the drum it was hit with.
# Sample set 1 (kick1, snare1, hh_cl1 / hh_op1, tom_hi1 / tom_lo1); all sampled kits ride `ride` and crash `crash`, which have
# real-sample starts (drum_sample.py).
_KIT1 = ("kick1", "snare1", "hh_cl1", "hh_op1", "ride", "crash",
         "tom_hi1", "tom_lo1")
_SYNTH = ("synth_kick", "synth_snare", "synth_hat", "synth_ohat", "synth_ride", "synth_crash",
          "synth_tom", "synth_tom_lo")
# Elec1: synth_drum / synth_cymbal variants, square tones (drive into the
# soft clip) and raw noise, drums/synth_el1_*.py; the Synth kit's hats and
# Elec2's closed hat on the ride lane.
_ELEC1 = ("synth_el1_kick", "synth_el1_snare", "synth_hat", "synth_ohat",
          "synth_el2_hat", "synth_el1_crash", "synth_el1_tom", "synth_el1_tom_lo")
# Elec2: drums/synth_el2_*.py, all on synth_drum's code, hats and cymbals
# too: high-passed noise plus two driven high sines (intermodulation =
# metallic ring).
_ELEC2 = tuple(f"synth_el2_{d}" for d in ("kick", "snare", "hat", "ohat", "ride", "crash", "tom", "tom_lo"))
# LoFi: drums/synth_lofi_*.py, all on synth_drum's code with its crush
# stage (sample hold ~12 kHz + coarse steps): dusty short kick, dark
# snare, dull gritty hats and cymbals.
_LOFI = tuple(f"synth_lofi_{d}" for d in ("kick", "snare", "hat", "ohat", "ride", "crash", "tom", "tom_lo"))
# Sample set 2 (kick2, snare2, hh_cl2 / hh_op2, tom_hi2 / tom_lo2)
_KIT2 = ("kick2", "snare2", "hh_cl2", "hh_op2", "ride", "crash",
         "tom_hi2", "tom_lo2")
NONE = "none"     # muted lane: no drum, its hits are skipped (DB_NO_DRUM)


KIT_DEFS = [
    # Kit knob order: acoustic -> mixed -> synth -> electronic -> lo-fi.
    # Names: Ac / Syn / El / Lo = sound source, the first part the drums,
    # the last the hats and cymbals (AcSyn = sampled drums, synth metal).
    # Lanes: kick snare chat ohat ride crash htom ltom; hats stay in pairs
    # (the closed one chokes the open one). Every kit must fit the kit
    # cache (~696 KB, check with preview.KitCache): long drums together
    # (synth_crash 2.7 s, synth_ride / Elec2 kick 1.6 s) cut the last
    # lanes short.
    # --- Acoustic (sampled): Ac1..Ac8, knob 0 = default ---
    ("Sample set 2", "Ac1", _KIT2),
    ("Sample set 1", "Ac2", _KIT1),
    ("kick4, set 2 snare + hats, set 1 toms", "Ac3",
     ("kick4", "snare2", "hh_cl2", "hh_op2", "ride", "crash", "tom_hi1", "tom_lo1")),
    ("Set 2 kick + toms, snare3, set 1 hats, set 2 closed hat as ride", "Ac4",
     ("kick2", "snare3", "hh_cl1", "hh_op1", "hh_cl2", "crash", "tom_hi2", "tom_lo2")),
    ("Set 1 kick, set 2 snare + toms, synth hats", "Ac5",
     ("kick1", "snare2", "synth_hat", "synth_ohat", "ride", "crash", "tom_hi2", "tom_lo2")),
    ("kick3, snare3, set 1 hats + toms", "Ac6",
     ("kick3", "snare3", "hh_cl1", "hh_op1", "ride", "crash", "tom_hi1", "tom_lo1")),
    ("kick4, snare3, synth hats, set 1 closed hat as ride, set 1 toms", "Ac7",
     ("kick4", "snare3", "synth_hat", "synth_ohat", "hh_cl1", "crash", "tom_hi1", "tom_lo1")),
    ("kick3, set 1 snare, set 2 hats + toms, set 1 closed hat as ride", "Ac8",
     ("kick3", "snare1", "hh_cl2", "hh_op2", "hh_cl1", "crash", "tom_hi2", "tom_lo2")),
    # --- Acoustic drums, electronic hats / cymbals (Ac + source) ---
    # sampled crash: the Synth crash's 2.7 s overflowed the cache (low tom cut)
    ("Set 2 drums, Synth hats + ride", "AcSyn",
     ("kick2", "snare2", "synth_hat", "synth_ohat", "synth_ride", "crash", "tom_hi2", "tom_lo2")),
    ("kick4, set 1 snare + toms, Elec2 hats + ride", "AcEl",
     ("kick4", "snare1", "synth_el2_hat", "synth_el2_ohat", "synth_el2_ride", "crash", "tom_hi1", "tom_lo1")),
    ("Set 1 kick, snare3, LoFi hats + ride", "AcLo",
     ("kick1", "snare3", "synth_lofi_hat", "synth_lofi_ohat", "synth_lofi_ride", "crash", "tom_hi1", "tom_lo1")),
    # --- Electronic drums, acoustic hats / cymbals (source + Ac) ---
    ("Synth drums, set 1 hats + sampled cymbals", "SynAc",
     ("synth_kick", "synth_snare", "hh_cl1", "hh_op1", "ride", "crash", "synth_tom", "synth_tom_lo")),
    ("Elec2 drums, set 2 hats + sampled cymbals", "ElAc",
     ("synth_el2_kick", "synth_el2_snare", "hh_cl2", "hh_op2", "ride", "crash", "synth_el2_tom", "synth_el2_tom_lo")),
    ("LoFi drums, set 1 hats + sampled cymbals", "LoAc",
     ("synth_lofi_kick", "synth_lofi_snare", "hh_cl1", "hh_op1", "ride", "crash", "synth_lofi_tom", "synth_lofi_tom_lo")),
    # --- Synth ---
    ("Synth", "Syn1", _SYNTH),
    # Elec2's ride + crash: the Synth ones with the 1.6 s Elec2 kick
    # overflowed the cache (low tom silent)
    ("Synth kit, Elec2 kick, ride + crash", "Syn2",
     ("synth_el2_kick", "synth_snare", "synth_hat", "synth_ohat", "synth_el2_ride", "synth_el2_crash", "synth_tom", "synth_tom_lo")),
    # --- Electronic ---
    ("Elec1", "Elec1", _ELEC1),
    ("Elec2", "Elec2", _ELEC2),
    ("Elec1 drums, LoFi hats + ride", "Elec3",
     ("synth_el1_kick", "synth_el1_snare", "synth_lofi_hat", "synth_lofi_ohat", "synth_lofi_ride", "synth_el1_crash", "synth_el1_tom", "synth_el1_tom_lo")),
    ("Elec1 kick + toms, Elec2 snare + metal", "Elec4",
     ("synth_el1_kick", "synth_el2_snare", "synth_el2_hat", "synth_el2_ohat", "synth_el2_ride", "synth_el2_crash", "synth_el1_tom", "synth_el1_tom_lo")),
    # --- Lo-fi ---
    ("LoFi (crushed synth)", "LoFi", _LOFI),
    ("LoFi kick + hats, Elec2 snare + toms", "LoBox",
     ("synth_lofi_kick", "synth_el2_snare", "synth_lofi_hat", "synth_lofi_ohat", "synth_el2_ride", "synth_lofi_crash", "synth_el2_tom", "synth_el2_tom_lo")),
]

# Load-probe builds (build.py --probe) leave kits out to fit the meter
# under the load limit: DRUMBOX_DROP_KITS = space-separated screen names.
_DROP = os.environ.get("DRUMBOX_DROP_KITS", "").split()
if _DROP:
    KIT_DEFS = [k for k in KIT_DEFS if k[1] not in _DROP]

KITS = [(name, short, {"drums": [DRUMS.get(d) for d in lanes], "names": lanes})
        for name, short, lanes in KIT_DEFS]

KIT_BAND = 1      # knob steps per kit: one step each, no repeated names
KNOB_MAX = (len(KITS) - 1) * KIT_BAND   # Kit knob "max" (set by build.py)


def knob_to_kit(v: int) -> int:
    """Same mapping as drumbox.c / GetString_DbKit: knob value -> kit index."""
    return max(0, min(v // KIT_BAND, len(KITS) - 1))


def kit_index(name_or_short: str) -> int:
    for i, (name, short, _f) in enumerate(KITS):
        if name_or_short.lower() in (name.lower(), short.lower()):
            return i
    raise ValueError(f"no kit {name_or_short!r}")


def used_drums() -> list[str]:
    """Drums in the kits, in order of first use (the ZDL holds only these)."""
    out: list[str] = []
    for _n, _s, lanes in KIT_DEFS:
        out += [d for d in lanes if d not in out and d != NONE]
    return out


def used_algos() -> list[str]:
    """Synth algorithms of the used drums; algorithm k is kind k + 1."""
    out: list[str] = []
    for d in used_drums():
        if DRUMS[d]["engine"] == "synth" and DRUMS[d]["algo"] not in out:
            out.append(DRUMS[d]["algo"])
    return out


def state_words(algo: str) -> int:
    return sum(DRUMS[algo]["code"].STATE.values())


# --- C headers -----------------------------------------------------------------

def _c_float(x: float) -> str:
    s = repr(float(x))
    return (s if ("." in s or "e" in s) else s + ".0") + "f"


def _rows(vals: list[str], per: int = 8) -> list[str]:
    return ["    " + ", ".join(vals[j:j + per]) + "," for j in range(0, len(vals), per)]


def _validate() -> None:
    assert all((e * 2731) >> 15 == e // 12 for e in range(128))
    if not 1 <= len(KITS) <= 100 // KIT_BAND:
        raise ValueError(f"need 1..{100 // KIT_BAND} kits (Kit knob max < 100), have {len(KITS)}")
    seen = set()
    for name, short, lanes in KIT_DEFS:
        if not (1 <= len(short) <= 5) or not all(33 <= ord(c) < 127 for c in short):
            raise ValueError(f"kit {name}: screen name {short!r} needs 1..5 printable ASCII")
        if short in seen:
            raise ValueError(f"kit screen name {short!r} used twice")
        if len(lanes) != len(LANES):
            raise ValueError(f"kit {name}: needs a drum for each of {LANES}")
        seen.add(short)
        for d in lanes:
            if d != NONE and d not in DRUMS:
                raise ValueError(f"kit {name}: no drum {d!r} in drums/")
    for d, m in DRUMS.items():
        if m["engine"] == "synth":
            code = m["code"]
            for fn in ("hit", "render"):
                if not callable(getattr(code, fn, None)):
                    raise ValueError(f"drums/{m['algo']}.py: no {fn}() (the Python mirror)")
            continue
        if len(m["partials"]) > MAX_PARTIALS:
            raise ValueError(f"drum {d}: more than {MAX_PARTIALS} partials")
        for env in [p["env"] for p in m["partials"]] + m["band_env"]:
            if len(env) != NUM_SEG or env[-1] != 0:
                raise ValueError(f"drum {d}: envelopes need {NUM_SEG} codes ending in 0")


def write_header(out_path: str | Path) -> None:
    """drumbox_kits.h (tables) and, next to it, drumbox_synth.h (the synth
    drums' code and its dispatch)."""
    _validate()
    from patterns import _getstr_table_lines
    drums = used_drums()
    algos = used_algos()
    syn_words = max([state_words(a) for a in algos] or [1])
    L = ["/* Generated by kits.py from the drums directory. Do not edit by hand. */",
         "#ifndef DRUMBOX_KITS_H", "#define DRUMBOX_KITS_H", "",
         f"#define DB_NUM_KITS {len(KITS)}", f"#define DB_KIT_BAND {KIT_BAND}",
         f"#define DB_NUM_LANES {len(LANES)}",
         f"#define DB_NUM_DRUMS {len(drums)}",
         "#define DB_NO_DRUM 255    /* kit_drums: muted lane */", f"#define DB_NUM_BANDS {NUM_BANDS}",
         f"#define DB_NUM_SEG {NUM_SEG}", f"#define DB_MAX_PARTIALS {MAX_PARTIALS}",
         f"#define DB_CHOKE_SAMPLES {CHOKE_SAMPLES}",
         f"#define DB_SYN_WORDS {syn_words}    /* state words of the biggest synth drum */", "",
         "/* All engine tables in one struct: the DSP takes its address once per",
         " * audio call (one .rela.dyn entry) instead of at every inlined use. */"]
    hdr, scale, env, pf, sp = [], [], [], [], []
    for d in drums:
        m = DRUMS[d]
        if m["engine"] == "synth":
            # kind = algorithm + 1; boff = first param in syn_idx
            hdr += ["0", "0", "0", str(len(sp)), "0", "0", str(algos.index(m["algo"]) + 1)]
            sp += [_c_float(m["params"][k]) for k in m["code"].PARAMS]
            scale += [_c_float(m["level"]), _c_float(m["level"])]
            continue
        boff = len(env)
        for row in m["enc_band"]:
            env += [str(b) for b in row]
        poff = len(env)
        for row in m["enc_part"]:
            env += [str(b) for b in row]
        hdr += [str(m["band_lo"]), str(m["band_hi"]), str(len(m["partials"])),
                str(boff), str(poff), str(len(pf) // 3), "0"]
        scale += [_c_float(m["band_scale"]), _c_float(m["part_scale"])]
        for p in m["partials"]:
            pf += [_c_float(p["f_end"]), _c_float(p["f_delta"]), _c_float(p["glide"])]
    # real-sample starts: sample index per drum (255 none) + per sample
    # header, floats and the packed codes
    dsmp, shdr, sf, sdat = [], [], [], []
    for d in drums:
        smp = DRUMS[d].get("smp")
        if smp is None:
            dsmp.append("255")
            continue
        dsmp.append(str(len(shdr) // 4))
        shdr += [str(len(sdat)), str(smp["bits"]), str(smp["fa"]), str(smp["fb"])]
        sf += [_c_float(x) for x in [smp["a"], smp["step0"], smp["gain"], smp["inv"]] + smp["mult"]]
        sdat += [str(b) for b in smp["data"]]
    if len(shdr) // 4 > 254 or len(sdat) > 65535:
        raise ValueError("over 254 samples or 64 KB of sample data")
    nsmp = len(shdr) // 4
    shdr, sf, sdat = shdr or ["0"] * 4, sf or ["0.0f"] * 8, sdat or ["0"]
    pf = pf or ["0.0f"]
    sp = sp or ["0.0f"]
    # synth params as a palette of the distinct floats + one index byte
    # each, padded so the DSP can always copy DB_SYN_NP of them
    syn_np = max([len(DRUMS[a]["code"].PARAMS) for a in algos] or [1])
    spal = list(dict.fromkeys(sp))
    if len(spal) > 256:
        raise ValueError("over 256 distinct synth params (one-byte palette index)")
    sidx = [str(spal.index(x)) for x in sp] + ["0"] * (syn_np - 1)
    env = env or ["0"]
    if len(env) > 65535 or len(sp) > 65535:
        raise ValueError("envelope or synth param table over 64 K entries (uint16 offsets)")
    if len(drums) > 255:
        raise ValueError("over 255 drums (kit_drums bytes, 255 = DB_NO_DRUM)")
    kd = [str(drums.index(d) if d != NONE else 255) for _n, _s, lanes in KIT_DEFS for d in lanes]
    fields = [
        ("int32_t", "grid", [str(g) for g in GRID], 8,
         "envelope grid (samples)"),
        ("float", "grid_inv", [_c_float(x) for x in GRID_INV], 5, "1 / segment length"),
        ("float", "frac12", [_c_float(x) for x in FRAC12], 6, "2^(-j / 12)"),
        ("float", "band_coef", [_c_float(x) for b in BANDS for x in b], 6,
         "band-pass b0, a1, a2 per band (" + ", ".join(str(f) for f in BAND_HZ) + " Hz)"),
        ("float", "drum_scale", scale, 4,
         "per drum: noise-band scale, partial scale (synth: level, level)"),
        ("float", "part_f", pf, 3, "partials: f_end, f_delta, glide"),
        ("float", "syn_pal", spal, 6, "distinct synth drum params"),
        ("uint8_t", "syn_idx", sidx, 16,
         "synth drum params (drums/<algo>.py PARAMS order) as syn_pal indices"),
        ("uint16_t", "drum_hdr", hdr, 7,
         "per drum (" + ", ".join(drums) + "): band_lo, band_hi, partials, band rows at"
         " (synth: params at), partial rows at, first partial, kind (0 resynth, k synth algorithm k)"),
        ("uint8_t", "kit_drums", kd, len(LANES), "per kit: drum per lane (" + " ".join(LANES) + ")"),
        ("int8_t", "dt", [str(x) for x in ENV_STEPS], 8, "envelope steps per nibble"),
        ("uint8_t", "env", env, 16, "compressed envelope rows (kits.encode_row)"),
        ("float", "smp_f", sf, 8, "per sample: a, step0, gain, 1 / fade length, step multipliers"),
        ("uint16_t", "smp_hdr", shdr, 4,
         "per sample: data at, bits, fade start, fade end (samples since the hit)"),
        ("uint8_t", "drum_smp", dsmp, 16, "per drum: its sample (255 none)"),
        ("uint8_t", "smp_data", sdat, 16, "packed DPCM codes (drum_sample.py)"),
    ]
    L[-2:-2] = [f"#define DB_SYN_NP {syn_np}    /* params of the biggest synth algorithm */",
                f"#define DB_NUM_SMP {nsmp}    /* drums with a real-sample start */",
                f"#define DB_SMP_DIV {SMP_DIV}", f"#define DB_SMP_STEP_MIN {_c_float(SMP_STEP_MIN)}",
                f"#define DB_SMP_STEP_MAX {_c_float(SMP_STEP_MAX)}"]
    L += ["typedef struct DBEng {"]
    L += [f"    {t} {name}[{len(v)}];    /* {c} */" for t, name, v, _per, c in fields]
    L += ["} DBEng;", "", "static const DBEng db_eng = {"]
    for _t, name, v, per, _c in fields:
        L += [f"    /* {name} */ {{"] + ["    " + r for r in _rows(v, per)] + ["    },"]
    L += ["};", ""]
    if KIT_BAND != 1:
        raise ValueError("DB_KIT_FROM_KNOB assumes one knob step per kit")
    # k >= 0: the DSP's knob value is clamped to 0..1 first, getstr's is unsigned
    L += [f"#define DB_KIT_FROM_KNOB(k) ((k) > {len(KITS) - 1} ? {len(KITS) - 1} : (k))", "",
          "/* Kit knob display: knob value -> kit name (see kits.py). */"]
    L += _getstr_table_lines("DB_KIT_GETSTR_BODY", "db_kit_names",
                             [k[1] for k in KIT_DEFS], "DB_KIT_FROM_KNOB(v)")
    L += ["", "#endif /* DRUMBOX_KITS_H */", ""]
    Path(out_path).write_text("\n".join(L))
    write_synth_header(Path(out_path).with_name("drumbox_synth.h"), algos)


def write_synth_header(out_path: Path, algos: list[str]) -> None:
    """drumbox_synth.h: param index macros, the algorithms' C files and the
    dispatch (an if chain, no switch: no jump table; each algorithm is
    inlined once per call site)."""
    L = ["/* Generated by kits.py from the drums directory (.py + .c files). Do not edit by hand.",
         " * Included by drumbox.c after its helpers and DBSyn. */",
         "#ifndef DRUMBOX_SYNTH_H", "#define DRUMBOX_SYNTH_H", ""]
    for a in algos:
        L += [f"/* {a} (kind {algos.index(a) + 1}) */"]
        L += [f"#define P_{a.upper()}_{k.upper()} {i}"
              for i, k in enumerate(DRUMS[a]["code"].PARAMS)]
        L += [f'#include "drums/{a}.c"', ""]
    hit = ["#define DB_SYN_HIT(kind, v, p, vel) do { \\"]
    ren = ["#define DB_SYN_RENDER(kind, v, p, y) do { \\", "    (y) = 0.0f; \\"]
    for i, a in enumerate(algos):
        kw = "if" if i == 0 else "else if"
        hit.append(f"    {kw} ((kind) == {i + 1}) db_syn_{a}_hit(v, p, vel); \\")
        ren.append(f"    {kw} ((kind) == {i + 1}) (y) = db_syn_{a}_render(v, p); \\")
    L += hit + ["} while (0)", ""] + ren + ["} while (0)", "", "#endif /* DRUMBOX_SYNTH_H */", ""]
    out_path.write_text("\n".join(L))


if __name__ == "__main__":
    write_header(HERE / "drumbox_kits.h")
    print(f"{len(KITS)} kits, {len(used_drums())} drums used of {len(DRUMS)} in drums/, "
          f"{len(used_algos())} synth algorithms")
