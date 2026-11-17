"""SongBox instruments: chords (keys) parts and bass parts, from
instruments/ (one file per instrument, as DrumBox's drums/).

Every instrument runs on one engine, instruments/engine.c (Python mirror
instruments/engine.py, the sample formula in its docstring): a wavetable voice
driven by a record, so a new instrument is data, not code. Instrument file
instruments/<id>.py: ROLE ("keys" or "bass"), LEVEL (mix gain), LOW / HIGH
(the wavetable's two halves: terms (harmonic, amplitude, "sin" | "cos")),
REC (engine.DEFAULTS fields that differ). A keys instrument can play the
bass too, at BASS_LEVEL (default LEVEL). A member "rec:table" plays the record of one file on
the table of another (a record-only file: ROLE, LEVEL, REC; a table-only
file: LOW, HIGH, GAIN), so one record can serve several tables and one
table several records (pluck.py on saw).

The Chords knob picks a KEYS part, the Bass knob a BASS part, each with a
level (value = part * LEVELS + level, "Or1:70"). c_header() writes
songbox_instr.h: the table terms (one table per table file used, first use
order), the records as byte indexes into a pool of distinct words (unpacked
into the state by ss_wt_build), per part (chords parts, then bass parts) its
id (record | table << 4), level and knob name, each as an initializer
(c_init) of a songbox.c SsConst member.
"""

from __future__ import annotations

import importlib.util
import struct
import sys
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
INST_DIR = HERE / "instruments"
for _d in (HERE, INST_DIR):                             # instrument files import inst_lib, engine
    if str(_d) not in sys.path:
        sys.path.append(str(_d))

# chords parts in Chords knob order: knob name -> member
KEYS = {
    "Or1": "organ1",                # Organ1: jazz organ
    "Or2": "organ2",                # Organ2: rock organ
    "EP": "epiano",                 # electric piano
    "Mrm": "marimba",
    "Klm": "kalimba:marimba",       # kalimba on the marimba's sine table
    "Syn": "saw",                   # synth: two detuned saws
    "Plk": "pluck:saw",             # pluck: the saw, -60 dB after 400 ms
}
# bass parts in Bass knob order: knob name -> member; a keys instrument plays its own bass
BASS = {
    "PB": "pbass",                  # precision bass (bass guitar)
    "O1": "organ1",                 # Organ1's bass
    "O2": "organ2",                 # Organ2's bass
    "Sy": "saw",                    # the synth's (saw) bass
    "Tr": "tri",                    # triangle bass
}
# Bass texts <= 5 characters ("PB:70"): two 6-character texts on
# neighbouring knobs run together on the screen ("PBs 70Or1 70")
# knob value = part * LEVELS + level; level shown x 5 ("PB:70"), gain =
# shown / 50 (50 = x1, as the old Volume); defaults Or1:70 / PB:70 (the
# old Volume 70)
LEVELS = 20
CHORDS_DEFAULT = 0 * LEVELS + 14
BASS_DEFAULT = 0 * LEVELS + 14
# every instrument level x MASTER: SongBox Volume 70 (now level 70) ~ DrumBox Volume 70
# (was 7x louder: SongBox 10 sounded like DrumBox 70), then +10 %, then +8 %
MASTER = 1.19 / 7.0
LVL_DIV = 4096                              # C levels: bytes, level = byte / LVL_DIV

for _t in list(KEYS) + list(BASS):          # knob text: name + ":70" in 7 characters
    assert _t.isascii() and 0 < len(_t) <= 4, f"part name {_t!r} over 4 characters"
for _t in BASS:                             # Bass text <= 5 characters (next to Chords)
    assert len(_t) <= 2, f"bass name {_t!r} over 2 characters"
# every chords x bass pair, by name: (keys member, bass member) (proto.py)
KITS = {f"{k}+{b}": (KEYS[k], BASS[b]) for k in KEYS for b in BASS}


def parts(member: str) -> tuple:
    """A set member "rec:table" (or "id" = both from one file) -> (rec, table)."""
    r, _c, t = member.partition(":")
    return r, (t or r)


def load(iid: str, what: str = "both"):
    """instruments/<iid>.py as a module, its contract checked for what:
    "rec" (ROLE, LEVEL, REC), "table" (LOW) or "both"."""
    path = INST_DIR / f"{iid}.py"
    spec = importlib.util.spec_from_file_location(f"songbox_inst_{iid}", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    need = {"rec": ("ROLE", "LEVEL", "REC"), "table": ("LOW",)}
    for name in need.get(what, need["rec"] + need["table"]):
        if not hasattr(mod, name):
            raise ValueError(f"instruments/{iid}.py: no {name}")
    if what != "table" and mod.ROLE not in ("keys", "bass"):
        raise ValueError(f"instruments/{iid}.py: ROLE {mod.ROLE!r}")
    return mod


def level(mod, role: str, tab=None) -> float:
    """Gain of an instrument in a role (LEVEL; a keys instrument plays the
    bass unchanged at BASS_LEVEL, default LEVEL) x the table file's GAIN (another file's table),
    x MASTER."""
    g = float(getattr(tab, "GAIN", 1.0)) if tab is not None and tab is not mod else 1.0
    if mod.ROLE == role:
        return float(mod.LEVEL) * g * MASTER
    if role == "bass" and mod.ROLE == "keys":
        return float(getattr(mod, "BASS_LEVEL", mod.LEVEL)) * g * MASTER
    raise ValueError(f"{mod.__name__}: can't play {role}")


def load_member(member: str) -> tuple:
    """(record module, table module) of a set member."""
    r, t = parts(member)
    mod = load(r, "rec" if r != t else "both")
    return mod, (load(t, "table") if r != t else mod)


def load_set(name: str) -> tuple:
    """(keys, bass) of a KITS name ("EP+PB"): render(v) + LEVEL in that role."""
    import engine
    out = []
    for role, member in zip(("keys", "bass"), KITS[name]):
        mod, tab = load_member(member)
        out.append(types.SimpleNamespace(render=engine.Instrument(mod, tab).render,
                                         LEVEL=level(mod, role, tab)))
    return tuple(out)


def c_init(name: str, what: str, rows: list) -> list:
    """#define name { rows }: a table's initializer for a member of songbox.c's
    SsConst (all tables in one struct: one base address, one relocation pair
    per function instead of one per table); what = the member, as a comment."""
    return [f"#define {name} {{   /* {what} */ \\"] + [f"    {r} \\" for r in rows] + ["}"]


def rec_words(rec: dict) -> list:
    """A record as its C words: float32 bits (engine.FLOATS), then engine.INTS."""
    import engine
    return ([struct.unpack("<I", struct.pack("<f", rec[k]))[0] for k in engine.FLOATS]
            + [rec[k] for k in engine.INTS])


def c_header(path: Path) -> None:
    import engine
    from inst_lib import WT_BITS, WT_N, WT_OFS
    kmem, bmem = list(KEYS.values()), list(BASS.values())
    members = kmem + bmem
    recs: list = []                                     # records used, first use order
    tabs: list = []                                     # tables used, first use order
    inst: dict = {}                                     # member -> Instrument
    mods: dict = {}                                     # record id -> module
    for m in members:
        r, t = parts(m)
        if m not in inst:
            mod, tab = load_member(m)
            inst[m] = engine.Instrument(mod, tab)
            mods[r] = mod
            if r in recs:                               # one C record for every table (as float32)
                first = inst[next(x for x in inst if parts(x)[0] == r)].rec
                assert rec_words(inst[m].rec) == rec_words(first), \
                    f"record {r!r} differs between its tables"
        for lst, x in ((recs, r), (tabs, t)):
            if x not in lst:
                lst.append(x)
    assert len(recs) <= 16 and len(tabs) <= 16, "part ids: record | table << 4"
    terms, ends = [], []
    for t in tabs:
        it = inst[next(m for m in members if parts(m)[1] == t)]
        terms += it.low
        ends.append(len(terms))
        terms += it.high
        ends.append(len(terms))

    def sid(m: str) -> int:
        r, t = parts(m)
        return recs.index(r) | tabs.index(t) << 4
    # rows per call: ~2,000 cycles of table build (a term ~4, a table half ~25)
    step = max(1, min(32, 2000 // (4 * len(terms) + 25 * len(ends))))
    L = ["/* Generated by build.py from sets.py + instruments/ -- do not edit.",
         " * Included by songbox.c before SsConst (tables as its initializers). */",
         "#ifndef SONGBOX_INSTR_H", "#define SONGBOX_INSTR_H",
         '#include "instruments/engine.h"',
         f"#define SS_WT_BITS {WT_BITS}u",
         f"#define SS_WT_N {WT_N}u            /* wavetable words per cycle (+ 1 guard word) */",
         f"#define SS_WT_OFS {WT_OFS:.1f}f       /* a word's high half offset (reads as a tiny float) */",
         "#define SS_WT_ROWS (SS_WT_N + 1u)    /* words per wavetable */",
         f"#define SS_NREC {len(recs)}u    /* instrument records (SsState.inst) */",
         f"#define SS_NTAB {len(tabs)}u    /* wavetables */",
         "#define SS_WT_WORDS (SS_NTAB * SS_WT_ROWS)   /* all wavetables */",
         f"#define SS_WT_STEP {step}u    /* rows built per call */",
         "/* table terms: amp (int16) << 16 | cos << 10 | harmonic; per table",
         " * the low half's, then the high half's (ends in ss_wt_end) */",
         f"#define SS_WT_NTERMS {len(terms)}u",
         f"#define SS_WT_NEND {len(ends)}u"]
    L += c_init("SS_WT_TERMS_INIT", "ss_wt_terms[SS_WT_NTERMS]",
                [f"{', '.join(f'0x{w:08X}u' for w in terms[i:i + 6])}," for i in range(0, len(terms), 6)])
    L += ["#define SS_WT_END_INIT { " + ", ".join(f"{x}u" for x in ends) + " }   /* ss_wt_end[SS_WT_NEND] */"]
    # records as byte indexes into a pool of distinct words (float bits /
    # uint32), unpacked into the state (SsState.inst) at the wavetable build
    words = []
    for rid in recs:
        words += rec_words(inst[next(m for m in members if parts(m)[0] == rid)].rec)
    pool = sorted(set(words))
    assert len(pool) <= 256, "record pool: byte indexes"
    nw = len(engine.FLOATS) + len(engine.INTS)
    L += [f"#define SS_INST_WORDS {nw}u    /* words per record (sizeof(SsInst) / 4) */",
          f"#define SS_NPOOL {len(pool)}u    /* distinct record words */"]
    L += c_init("SS_INST_POOL_INIT", "ss_inst_pool[SS_NPOOL]",
                [f"{', '.join(f'0x{w:08X}u' for w in pool[i:i + 6])}," for i in range(0, len(pool), 6)])
    # a default record (each word's most common index), then per record
    # the indexes of the words that differ from it (bit j of its mask)
    ixs = [[pool.index(w) for w in words[i * nw:(i + 1) * nw]] for i in range(len(recs))]
    dflt = [max(sorted({r[j] for r in ixs}), key=lambda x: sum(r[j] == x for r in ixs))
            for j in range(nw)]
    assert nw <= 32, "record mask: one word"
    masks = [sum(1 << j for j in range(nw) if r[j] != dflt[j]) for r in ixs]
    rows = ["/* default */ " + ", ".join(f"{x}u" for x in dflt) + ","]
    for i, rid in enumerate(recs):
        rows.append(f"/* {i}: {rid} ({mods[rid].ROLE}) */ "
                    + "".join(f"{x}u, " for j, x in enumerate(ixs[i]) if masks[i] >> j & 1))
    nb = nw + sum(bin(m).count("1") for m in masks)
    L += [f"#define SS_NINSTB {nb}u    /* default record + differing words */"]
    L += c_init("SS_INST_IDX_INIT", "ss_inst_idx[SS_NINSTB]", rows)
    L += ["#define SS_INST_MASK_INIT { " + ", ".join(f"0x{m:08X}u" for m in masks)
          + " }   /* ss_inst_mask[SS_NREC]: words differing from the default */"]

    def lvl(m: str, role: str) -> float:
        mod, tab = load_member(m)
        return level(mod, role, tab)
    lk = [lvl(m, "keys") for m in kmem]
    lb = [lvl(m, "bass") for m in bmem]
    qk = [round(x * LVL_DIV) for x in lk]
    qb = [round(x * LVL_DIV) for x in lb]
    if not all(0 < q < 256 for q in qk + qb):
        raise ValueError(f"levels don't fit a byte at 1/{LVL_DIV}: {lk} {lb}")
    lvl_err = max(abs(q / LVL_DIV / x - 1.0) for q, x in zip(qk + qb, lk + lb))
    names = list(KEYS) + list(BASS)
    top = max(len(KEYS), len(BASS)) * LEVELS
    assert LEVELS == 20 and all((v * 205) >> 12 == v // LEVELS for v in range(top)), \
        "songbox.c: part = v * 205 >> 12 (v / 20)"
    L += ["", f"#define SS_LEVELS {LEVELS}u    /* levels per part (Chords / Bass value = part * SS_LEVELS + level) */",
          f"#define SS_NKEYI {len(kmem)}u    /* chords parts (Chords knob) */",
          f"#define SS_NBASSI {len(bmem)}u    /* bass parts (Bass knob) */",
          "/* part names, NUL-terminated (the end of songbox.c's text pool):",
          " * chords parts, then bass parts */",
          "#define SS_INSTR_NAMES_INIT "
          + " ".join('"' + t + "\\0" + '"' for t in names[:-1]) + f' "{names[-1]}"',
          "/* per part (chords parts, then bass parts): instrument (record |",
          " * wavetable << 4) and level: ss_part_id / ss_part_lvl[SS_NKEYI + SS_NBASSI]",
          " * (" + ", ".join(names) + ")",
          f" * (level * SS_LVL_DIV, quantized: <= {lvl_err * 100:.2f} % off) */",
          f"#define SS_LVL_DIV {LVL_DIV}.0f",
          "#define SS_PART_ID_INIT { " + ", ".join(f"{sid(m)}u" for m in members) + " }",
          "#define SS_PART_LVL_INIT { " + ", ".join(f"{q}u" for q in qk + qb) + " }",
          "", "#endif", ""]
    path.write_bytes("\r\n".join(L).encode("ascii"))
