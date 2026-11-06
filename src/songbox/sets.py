"""SongBox instrument sets: one keys and one bass instrument each, from
instruments/ (one file per instrument, as DrumBox's drums/).

Instrument file instruments/<id>.py: ROLE ("keys" or "bass"), LEVEL (mix
gain in that role), optional BASS_LEVEL (a keys instrument that may also
play the bass: its gain there; its render(v, bass) / C ss_i_<id>(v, bass)
get a flag, 1 on the bass voice), CONST (per-sample constants, computed once
by inst_lib.coef() / lp_coef() / rise()), render(v) -> one sample of voice
v (inst_lib.Voice). Its C mirror instruments/<id>.c, line by line: `static
inline float ss_i_<id>(SsVoice *v)` reading its constants as
SS_I_<ID>_<NAME> (helpers from instruments/inst_lib.h), branch-free,
plus `static inline void ss_h_<id>(SsVoice *v)`: a new note (v->trig, set
by ss_trigger; ss_run clears it before the samples). Optional TABLE = True:
a wavetable of inst_lib.WT_N + 1 words (one per TABLE instrument, one after
the other), built once in ctx[3] (songbox.c, a few rows per call; row i =
word i of every table, sharing one sine s = ss_sin(i / SS_WT_N)) from C
`static inline uint32_t ss_t_<id>(float s)` (Python table_word(s); each
32-bit word must read as a tiny float: high half + WT_OFS); instead of
ss_i_<id>, one sample in three
steps run as three loops over a piece (cl6x at -ms3 only overlaps samples in
loops with short chains): `ss_ia_<id>(SsVoice *v, uint32_t *si, float *sf)`
(the values carried from sample to sample; leaves 2 + 3 values), `float
ss_ib_<id>(const uint32_t *si, const float *sf, const uint32_t *t)` (into
sf[1]) and `float ss_ic_<id>(SsVoice *v, float x, const float *sf)` (may
carry a short chain of its own, e.g. a DC blocker; Python: render does all
three and builds its own table copy). Optional PIECE = True (TABLE
instruments): C `static inline void ss_p_<id>(SsVoice *v, uint32_t n, float g)`
runs once per piece of n samples before the three loops (slow things such
as an LFO; Python: render computes them per sample); g is the voice gain,
and ss_ic_<id> then returns its sample already x g (ss_run doesn't
multiply).

The Instr knob (INSTR): per set, in SETS order, both / bass only / chords
only, then Mute. c_header() writes songbox_instr.h (songbox.c includes it
after SsVoice): the constants and .c files of every instrument used, once
each; ss_run(v, id, bass, ..) running instrument id over a run of samples
(one if chain, each instrument inlined once, so one instrument can play
keys in one set and bass in another at no code cost); per set the keys / bass instrument ids and
levels; the Instr table (set << 2 | 2 = no bass | 1 = no chords) and its
knob texts.
"""

from __future__ import annotations

import importlib.util
import sys
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
INST_DIR = HERE / "instruments"

# name: (knob text, prefix for " Bs" / " Ch" (bass / chords only), keys, bass);
# the first set = Instr 0 (the default)
SETS = {
    "epiano": ("EPiano", "EP", "epiano", "epiano"),       # bass instruments left out for now:
    "jazz": ("JzzOrg", "Jaz", "organ_jazz", "organ_jazz"),  # each set's keys play its bass too
    "rock": ("RckOrg", "Rck", "organ_rock", "organ_rock"),
}
NO_CHORDS, NO_BASS = 1, 2
# every instrument level x MASTER: SongBox Volume 70 ~ DrumBox Volume 70
# (was 7x louder: SongBox 10 sounded like DrumBox 70), then +10 %
MASTER = 1.1 / 7.0


def instr_values() -> list:
    """Instr knob values: (knob text, set index, NO_* bits)."""
    out = []
    for i, (text, pre, _k, _b) in enumerate(SETS.values()):
        out += [(text, i, 0), (pre + " Bs", i, NO_CHORDS), (pre + " Ch", i, NO_BASS)]
    out.append(("Mute", 0, NO_BASS | NO_CHORDS))
    for text, _s, _m in out:
        assert len(text) <= 6, f"Instr text {text!r} over 6 characters (7 don't fit)"
    return out


INSTR = instr_values()


def load(iid: str):
    """instruments/<iid>.py as a module, its contract checked."""
    if str(HERE) not in sys.path:
        sys.path.insert(0, str(HERE))                   # instrument files import inst_lib
    path = INST_DIR / f"{iid}.py"
    spec = importlib.util.spec_from_file_location(f"songbox_inst_{iid}", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for name in ("ROLE", "LEVEL", "CONST", "render"):
        if not hasattr(mod, name):
            raise ValueError(f"instruments/{iid}.py: no {name}")
    if getattr(mod, "TABLE", False) and hasattr(mod, "BASS_LEVEL"):
        raise ValueError(f"instruments/{iid}.py: TABLE with BASS_LEVEL not supported")
    if mod.ROLE not in ("keys", "bass"):
        raise ValueError(f"instruments/{iid}.py: ROLE {mod.ROLE!r}")
    return mod


def level(mod, role: str) -> float:
    """Gain of an instrument in a role (LEVEL in its own, BASS_LEVEL as bass;
    a keys instrument without BASS_LEVEL plays the bass unchanged at LEVEL),
    x MASTER."""
    if mod.ROLE == role:
        return float(mod.LEVEL) * MASTER
    if role == "bass" and hasattr(mod, "BASS_LEVEL"):
        return float(mod.BASS_LEVEL) * MASTER
    if role == "bass" and mod.ROLE == "keys":
        return float(mod.LEVEL) * MASTER
    raise ValueError(f"{mod.__name__}: can't play {role}")


def load_set(name: str) -> tuple:
    """(keys, bass) of set name: render(v) + LEVEL in that role."""
    out = []
    for role, iid in zip(("keys", "bass"), SETS[name][2:]):
        mod = load(iid)
        if hasattr(mod, "BASS_LEVEL"):                  # takes the bass flag
            render = (lambda m, b: lambda v: m.render(v, b))(mod, role == "bass")
        else:
            render = mod.render
        out.append(types.SimpleNamespace(render=render, LEVEL=level(mod, role)))
    return tuple(out)


def cfloat(v: float) -> str:
    """v as a C float literal (6900 -> 6900.0f)."""
    s = f"{v:.9g}"
    return s + ("f" if "." in s or "e" in s else ".0f")


def c_header(path: Path) -> None:
    L = ["/* Generated by build.py from sets.py + instruments/ -- do not edit.",
         " * Included by songbox.c after SsVoice. */",
         "#ifndef SONGBOX_INSTR_H", "#define SONGBOX_INSTR_H",
         '#include "instruments/inst_lib.h"']
    sets = list(SETS.values())
    ids: list = []                                      # instruments used, first use order
    for s in sets:
        for iid in s[2:]:
            if iid not in ids:
                ids.append(iid)
    mods = {iid: load(iid) for iid in ids}
    tab = [iid for iid in ids if getattr(mods[iid], "TABLE", False)]   # one table each, in this order
    from inst_lib import WT_N, WT_OFS
    from inst_lib import WT_BITS
    L += [f"#define SS_WT_BITS {WT_BITS}u", f"#define SS_WT_N {WT_N}u            /* wavetable words per cycle (+ 1 guard word) */",
          f"#define SS_WT_OFS {WT_OFS:.1f}f       /* a word's high half offset (reads as a tiny float) */",
          "#define SS_WT_ROWS (SS_WT_N + 1u)    /* words per wavetable */",
          f"#define SS_WT_WORDS ({len(tab)}u * SS_WT_ROWS)   /* all wavetables */"]
    lk = [level(mods[s[2]], "keys") for s in sets]
    lb = [level(mods[s[3]], "bass") for s in sets]
    for iid in ids:
        if not (INST_DIR / f"{iid}.c").exists():
            raise ValueError(f"no instruments/{iid}.c")
        L += ["", f"/* {ids.index(iid)}: {iid} ({mods[iid].ROLE}) */"]
        L += [f"#define SS_I_{iid.upper()}_{k.upper()} {cfloat(v)}" for k, v in mods[iid].CONST.items()]
        L.append(f'#include "instruments/{iid}.c"')
    L += ["", f"#define SS_NSETS {len(sets)}u",
          f"#define SS_NINSTR {len(INSTR)}u    /* Instr knob values */",
          "#define SS_IN_NOCHORDS 1u", "#define SS_IN_NOBASS 2u",
          "/* per Instr value: set << 2 | SS_IN_NOBASS | SS_IN_NOCHORDS */",
          "static const uint8_t ss_instr[SS_NINSTR] = { "
          + ", ".join(f"{s << 2 | m}u" for _t, s, m in INSTR) + " };",
          "static const char ss_instr_names[SS_NINSTR * 8u] =",
          "    " + " ".join('"' + t + "\\0" * (8 - len(t)) + '"' for t, _s, _m in INSTR) + ";",
          "/* per set: keys and bass instrument (ss_run id) and their levels */",
          "static const uint8_t ss_set_keys[SS_NSETS] = { "
          + ", ".join(f"{ids.index(s[2])}u" for s in sets) + " };",
          "static const uint8_t ss_set_bass[SS_NSETS] = { "
          + ", ".join(f"{ids.index(s[3])}u" for s in sets) + " };",
          "static const float ss_lvl_keys[SS_NSETS] = { " + ", ".join(f"{x:.9g}f" for x in lk) + " };",
          "static const float ss_lvl_bass[SS_NSETS] = { " + ", ".join(f"{x:.9g}f" for x in lb) + " };",
          "", "/* wavetable row i: word i of each table (t: the tables one after the",
          " * other), from one shared sine */",
          "ZM_INLINE(ss_wt_row)",
          "static inline void ss_wt_row(uint32_t *t, uint32_t i)", "{"]
    if tab:
        L += ["    float s = ss_sin((float)(int)i * (1.0f / (float)SS_WT_N));"]
    L += [f"    t[{f'{k}u * SS_WT_ROWS + ' if k else ''}i] = ss_t_{iid}(s);" for k, iid in enumerate(tab)]
    if not tab:
        L += ["    (void)t; (void)i;"]
    L += ["}",
          
          "", "/* voice v by instrument id: a new note (v->trig) first, then n samples",
          " * x g added to acc[0..n), the gate counting down by ginc per sample (wt:",
          " * the wavetables; a TABLE instrument runs as three loops through the",
          " * scratch si[2n] / sf[3n]: ss_ia_<id>, ss_ib_<id>, ss_ic_<id>). The",
          " * voice is copied to w for the loop (its fields stay in registers); one",
          " * if chain (no switch), each instrument inlined once. */",
          "ZM_INLINE(ss_run)",
          "static inline void ss_run(SsVoice *v, uint32_t id, uint32_t bass, float g, float ginc,",
          "                          float *restrict acc, uint32_t n, const uint32_t *restrict wt,",
          "                          uint32_t *restrict si, float *restrict sf)", "{",
          "    SsVoice w;", "    uint32_t i;", "    SS_VCOPY(w, *v);", "    if (w.trig) {", "        w.trig = 0u;"]

    def call(iid):
        if hasattr(mods[iid], "BASS_LEVEL"):
            return f"ss_i_{iid}(&w, bass)"
        return f"ss_i_{iid}(&w, wt)" if iid in tab else f"ss_i_{iid}(&w)"
    for i, iid in enumerate(ids):
        cond = f"if (id == {i}u) " if i < len(ids) - 1 else ""
        L.append(f"        {'else ' if i else ''}{cond}ss_h_{iid}(&w);")
    L.append("    }")
    for i, iid in enumerate(ids):
        cond = f"if (id == {i}u) " if i < len(ids) - 1 else ""
        L += [f"    {'} else ' if i else ''}{cond}{{"]
        if iid in tab:
            t = f"wt + {tab.index(iid)}u * SS_WT_ROWS" if tab.index(iid) else "wt"
            if getattr(mods[iid], "PIECE", False):
                L.append(f"        ss_p_{iid}(&w, n, g);")
            mg = "" if getattr(mods[iid], "PIECE", False) else " * g"    # PIECE: ic applies g
            L += [f"        for (i = 0u; i < n; ++i) {{ ss_ia_{iid}(&w, &si[2u * i], &sf[3u * i]); w.gate -= ginc; }}",
                  f"        for (i = 0u; i < n; ++i) sf[3u * i + 1u] = ss_ib_{iid}(&si[2u * i], &sf[3u * i], {t});",
                  f"        for (i = 0u; i < n; ++i) acc[i] += ss_ic_{iid}(&w, sf[3u * i + 1u], &sf[3u * i]){mg};"]
        else:
            L += [f"        for (i = 0u; i < n; ++i) {{ acc[i] += {call(iid)} * g; w.gate -= ginc; }}"]
    L += ["    }", "    SS_VCOPY(*v, w);", "}", "", "#endif", ""]
    path.write_bytes("\r\n".join(L).encode("ascii"))
