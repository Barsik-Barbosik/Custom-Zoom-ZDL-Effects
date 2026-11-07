"""SongBox instrument sets: one keys and one bass instrument each, from
instruments/ (one file per instrument, as DrumBox's drums/).

Every instrument runs on one engine, instruments/engine.c (Python mirror
instruments/engine.py, the sample formula in its docstring): a wavetable voice
driven by a record, so a new instrument is data, not code. Instrument file
instruments/<id>.py: ROLE ("keys" or "bass"), LEVEL (mix gain), LOW / HIGH
(the wavetable's two halves: terms (harmonic, amplitude, "sin" | "cos")),
REC (engine.DEFAULTS fields that differ). A keys instrument plays its set's
bass too, at LEVEL.

The Instr knob (INSTR): a list of (knob text, set, who plays: both / bass
only / chords only / nobody = Mute). c_header() writes songbox_instr.h: the table terms, the
records ss_inst_tab (one per instrument used, in first use order; ss_run's
table = wt + id x SS_WT_ROWS), per set the keys / bass instrument ids and
levels, the Instr table (set << 2 | 2 = no bass | 1 = no chords) and its knob
texts, each as an initializer (c_init) of a songbox.c SsConst member.
"""

from __future__ import annotations

import importlib.util
import sys
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
INST_DIR = HERE / "instruments"
for _d in (HERE, INST_DIR):                             # instrument files import inst_lib, engine
    if str(_d) not in sys.path:
        sys.path.append(str(_d))

# name: (keys, bass) instruments; a keys instrument can play its own bass
SETS = {
    "epiano": ("epiano", "pbass"),
    "organ1": ("organ1", "pbass"),
    "organ1own": ("organ1", "organ1"),
    "organ2": ("organ2", "pbass"),
    "organ2own": ("organ2", "organ2"),
}
NO_CHORDS, NO_BASS = 1, 2
# Instr knob values in knob order: (knob text, set, NO_* bits)
INSTR_LIST = (
    ("Organ1", "organ1own", 0),             # all notes on Organ1
    ("Org1+B", "organ1", 0),
    ("Org1Ch", "organ1", NO_BASS),
    ("Organ2", "organ2own", 0),             # all notes on Organ2
    ("Org2+B", "organ2", 0),
    ("Org2Ch", "organ2", NO_BASS),
    ("EPno+B", "epiano", 0),
    ("EPnoCh", "epiano", NO_BASS),
    ("Bass", "epiano", NO_CHORDS),          # the precision bass alone
    ("OrgBs", "organ1own", NO_CHORDS),      # Organ1's own bass alone
    ("Mute", "epiano", NO_BASS | NO_CHORDS),
)
INSTR_DEFAULT = 1                           # Org1+B
# every instrument level x MASTER: SongBox Volume 70 ~ DrumBox Volume 70
# (was 7x louder: SongBox 10 sounded like DrumBox 70), then +10 %, then +8 %
MASTER = 1.19 / 7.0


def instr_values() -> list:
    """Instr knob values: (knob text, set index, NO_* bits)."""
    names = list(SETS)
    out = [(text, names.index(st), m) for text, st, m in INSTR_LIST]
    for text, _s, _m in out:
        assert len(text) <= 6, f"Instr text {text!r} over 6 characters (7 don't fit)"
    return out


INSTR = instr_values()


def load(iid: str):
    """instruments/<iid>.py as a module, its contract checked."""
    path = INST_DIR / f"{iid}.py"
    spec = importlib.util.spec_from_file_location(f"songbox_inst_{iid}", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for name in ("ROLE", "LEVEL", "LOW", "REC"):
        if not hasattr(mod, name):
            raise ValueError(f"instruments/{iid}.py: no {name}")
    if mod.ROLE not in ("keys", "bass"):
        raise ValueError(f"instruments/{iid}.py: ROLE {mod.ROLE!r}")
    return mod


def level(mod, role: str) -> float:
    """Gain of an instrument in a role (LEVEL; a keys instrument plays the
    bass unchanged at LEVEL), x MASTER."""
    if mod.ROLE == role:
        return float(mod.LEVEL) * MASTER
    if role == "bass" and mod.ROLE == "keys":
        return float(mod.LEVEL) * MASTER
    raise ValueError(f"{mod.__name__}: can't play {role}")


def load_set(name: str) -> tuple:
    """(keys, bass) of set name: render(v) + LEVEL in that role."""
    import engine
    out = []
    for role, iid in zip(("keys", "bass"), SETS[name]):
        mod = load(iid)
        out.append(types.SimpleNamespace(render=engine.Instrument(mod).render, LEVEL=level(mod, role)))
    return tuple(out)


def c_init(name: str, what: str, rows: list) -> list:
    """#define name { rows }: a table's initializer for a member of songbox.c's
    SsConst (all tables in one struct: one base address, one relocation pair
    per function instead of one per table); what = the member, as a comment."""
    return [f"#define {name} {{   /* {what} */ \\"] + [f"    {r} \\" for r in rows] + ["}"]


def cfloat(v: float) -> str:
    """v as a C float literal (6900 -> 6900.0f)."""
    s = f"{v:.9g}"
    return s + ("f" if "." in s or "e" in s else ".0f")


def c_header(path: Path) -> None:
    import engine
    from inst_lib import WT_BITS, WT_N, WT_OFS
    sets = list(SETS.values())
    ids: list = []                                      # instruments used, first use order
    for st in sets:
        for iid in st:
            if iid not in ids:
                ids.append(iid)
    mods = {iid: load(iid) for iid in ids}
    inst = {iid: engine.Instrument(mods[iid]) for iid in ids}
    terms, ends = [], []
    for iid in ids:
        terms += inst[iid].low
        ends.append(len(terms))
        terms += inst[iid].high
        ends.append(len(terms))
    # rows per call: ~4,000 cycles of table build (a term ~4, a table half ~25)
    step = max(1, min(32, 4000 // (4 * len(terms) + 25 * len(ends))))
    L = ["/* Generated by build.py from sets.py + instruments/ -- do not edit.",
         " * Included by songbox.c before SsConst (tables as its initializers). */",
         "#ifndef SONGBOX_INSTR_H", "#define SONGBOX_INSTR_H",
         '#include "instruments/engine.h"',
         f"#define SS_WT_BITS {WT_BITS}u",
         f"#define SS_WT_N {WT_N}u            /* wavetable words per cycle (+ 1 guard word) */",
         f"#define SS_WT_OFS {WT_OFS:.1f}f       /* a word's high half offset (reads as a tiny float) */",
         "#define SS_WT_ROWS (SS_WT_N + 1u)    /* words per wavetable */",
         f"#define SS_NINST {len(ids)}u    /* instruments (ss_inst_tab, one table each) */",
         "#define SS_WT_WORDS (SS_NINST * SS_WT_ROWS)   /* all wavetables */",
         f"#define SS_WT_STEP {step}u    /* rows built per call */",
         "/* table terms: amp (int16) << 16 | cos << 10 | harmonic; per instrument",
         " * the low half's, then the high half's (ends in ss_wt_end) */",
         f"#define SS_WT_NTERMS {len(terms)}u",
         f"#define SS_WT_NEND {len(ends)}u"]
    L += c_init("SS_WT_TERMS_INIT", "ss_wt_terms[SS_WT_NTERMS]",
                [f"{', '.join(f'0x{w:08X}u' for w in terms[i:i + 6])}," for i in range(0, len(terms), 6)])
    L += ["#define SS_WT_END_INIT { " + ", ".join(f"{x}u" for x in ends) + " }   /* ss_wt_end[SS_WT_NEND] */"]
    rows = []
    for iid in ids:
        r = inst[iid].rec
        rows.append(f"{{ /* {ids.index(iid)}: {iid} ({mods[iid].ROLE}) */")
        rows.append("    " + ", ".join(cfloat(r[k]) for k in engine.FLOATS) + ",")
        rows.append("    " + ", ".join(f"{r[k]}u" for k in engine.INTS) + " },")
    L += c_init("SS_INST_TAB_INIT", "ss_inst_tab[SS_NINST]", rows)
    lk = [level(mods[st[0]], "keys") for st in sets]
    lb = [level(mods[st[1]], "bass") for st in sets]
    L += ["", f"#define SS_NSETS {len(sets)}u",
          f"#define SS_NINSTR {len(INSTR)}u    /* Instr knob values */",
          "#define SS_IN_NOCHORDS 1u", "#define SS_IN_NOBASS 2u",
          "/* ss_instr[SS_NINSTR]: per Instr value set << 2 | SS_IN_NOBASS | SS_IN_NOCHORDS */",
          "#define SS_INSTR_INIT { "
          + ", ".join(f"{st << 2 | m}u" for _t, st, m in INSTR) + " }",
          "/* Instr names, NUL-terminated (the end of songbox.c's text pool) */",
          "#define SS_INSTR_NAMES_INIT "
          + " ".join('"' + t + "\\0" + '"' for t, _s, _m in INSTR[:-1]) + f' "{INSTR[-1][0]}"',
          "/* per set: keys and bass instrument (ss_inst_tab index) and their levels:",
          " * ss_set_keys / ss_set_bass[SS_NSETS], ss_lvl_keys / ss_lvl_bass[SS_NSETS] */",
          "#define SS_SET_KEYS_INIT { "
          + ", ".join(f"{ids.index(st[0])}u" for st in sets) + " }",
          "#define SS_SET_BASS_INIT { "
          + ", ".join(f"{ids.index(st[1])}u" for st in sets) + " }",
          "#define SS_LVL_KEYS_INIT { " + ", ".join(f"{x:.9g}f" for x in lk) + " }",
          "#define SS_LVL_BASS_INIT { " + ", ".join(f"{x:.9g}f" for x in lb) + " }",
          "", "#endif", ""]
    path.write_bytes("\r\n".join(L).encode("ascii"))
