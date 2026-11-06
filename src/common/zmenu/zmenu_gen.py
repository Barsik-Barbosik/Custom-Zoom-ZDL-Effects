"""zmenu layout generator: menus -> <effect>_zmenu.h + the menu knobs' params.

    from zmenu_gen import Menu, Field, generate
    menus = [
        Menu("Bar", records=4, fields=[Field("Beat", 128), Field("Chord", names=CHORDS)]),
        Menu("Param", params=[Field("Level", 101, default=50), Field("Wave", names=["Tri", "Sqr"])]),
    ]
    knob_params = generate(menus, HERE / "fx_zmenu.h", desc="_Fx_SFX_X_Desc")

A record menu: selector (names "Bar1".. or `record_names`) + one knob per
field; every record has all fields. A parameter menu: selector (the param
names) + one Value knob renamed to the selected param; each param has its own
steps / default / names. Menus take knobs 1.. in order, selector first.

Every (record, field) is a slot of ceil(log2(steps)) bits, packed in order
into Floppy's 9 knob fields as one bit stream (a value may span fields).
Field widths: `widths="common"` = the smaller of G1on and MS per knob (one
layout for both; 106 bits while MS is assumed equal to G1on), "g1on" = 106 bits.
`packing="radix"`: mixed radix instead, all slots as one number (slot i =
digit (value - default) mod steps_i), so a slot costs log2(steps) bits;
steps <= 256 (the DSP divides limb by limb with floats, zmenu.h).

Floppy (the storage effect) is built by src/floppy/build.py, the compile
helpers are in build/zdl_build.py.
"""

from __future__ import annotations

import math
import struct
import sys
import zlib
from dataclasses import dataclass, field as dc_field
from pathlib import Path

FLOPPY_FXID = 522                                          # gid 7 (SFX)
WIDTHS = {
    "g1on": (13, 13, 13, 8, 8, 8, 8, 27, 8),               # HW (MenuPrb stages 10-21), all exact
    # MS pedals: assumed the same as the G1on (untested; Ziddle's patch.rs
    # says 12,12,12,7,7,7,7,11,7, but its G1on widths were wrong too)
    "ms": (13, 13, 13, 8, 8, 8, 8, 27, 8),
}
WIDTHS["common"] = tuple(min(a, b) for a, b in zip(WIDTHS["g1on"], WIDTHS["ms"]))
FLOPPY_MAXES = WIDTHS["g1on"]                              # Floppy declares the G1on field maxes
KNOB_MAX = 150        # menu knobs are read through params[] (LineSel law 0..150)
NAME_CHARS = 5        # knob text on the G1on screen


@dataclass
class Field:
    name: str
    steps: int = 0                      # 0 = len(names)
    default: int = 0
    names: list[str] | None = None

    def __post_init__(self) -> None:
        if not self.steps:
            if not self.names:
                raise ValueError(f"field {self.name}: steps or names needed")
            self.steps = len(self.names)
        if self.names is not None and len(self.names) != self.steps:
            raise ValueError(f"field {self.name}: {len(self.names)} names for {self.steps} steps")
        if not 0 <= self.default < self.steps:
            raise ValueError(f"field {self.name}: default {self.default} outside 0..{self.steps - 1}")


@dataclass
class Menu:
    name: str
    records: int = 0                    # record menu: number of records
    fields: list[Field] = dc_field(default_factory=list)
    params: list[Field] | None = None   # parameter menu instead
    record_names: list[str] | None = None

    @property
    def is_param(self) -> bool:
        return self.params is not None

    def nfld(self) -> int:
        return 1 if self.is_param else len(self.fields)

    def nrec(self) -> int:
        return len(self.params) if self.is_param else self.records

    def slot_fields(self) -> list[list[Field]]:
        """Fields per record."""
        if self.is_param:
            return [[p] for p in self.params]
        return [list(self.fields) for _ in range(self.records)]

    def sel_names(self) -> list[str]:
        if self.is_param:
            return [p.name for p in self.params]
        if self.record_names:
            return list(self.record_names)
        return [f"{self.name[:NAME_CHARS - len(str(self.records))]}{i + 1}" for i in range(self.records)]


def _bits(steps: int) -> int:
    return max(1, (steps - 1).bit_length())


def _c_name(s: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in s).upper()


def generate(menus: list[Menu], out_h: Path, desc: str, widths: str = "common",
             report: bool = True, packing: str = "bits") -> list[dict]:
    """Write the layout header; return the manifest params of the menu knobs."""
    if packing not in ("bits", "radix"):
        raise ValueError(f"packing {packing!r}: bits or radix")
    radix = packing == "radix"
    wid = WIDTHS[widths]
    starts = [sum(wid[:i]) for i in range(9)]
    capacity = sum(wid)

    names: list[str] = []                       # flat 5-char name table
    def add_names(lst: list[str]) -> int:
        for n in lst:
            if not n or len(n) > NAME_CHARS or not n.isascii():
                raise ValueError(f"name {n!r}: 1..{NAME_CHARS} ASCII chars")
        # reuse an identical list
        for i in range(len(names) - len(lst) + 1):
            if names[i:i + len(lst)] == lst:
                return i
        names.extend(lst)
        return len(names) - len(lst)

    menu_rows, slot_rows, kmax, params, macros = [], [], [], [], []
    pos = 0
    product = 1                                 # radix: number of storable states
    knob = 0
    for m in menus:
        nrec, nfld = m.nrec(), m.nfld()
        if nrec < 1 or nfld < 1:
            raise ValueError(f"menu {m.name}: empty")
        sel = knob
        knob += 1 + nfld
        if knob > 9:
            raise ValueError(f"menu {m.name}: past knob 9")
        slot0 = len(slot_rows)
        sel_names = add_names(m.sel_names())
        menu_rows.append((sel, nfld, nrec, int(m.is_param), slot0, sel_names))
        per_rec = m.slot_fields()
        fmax = [max(per_rec[r][f].steps for r in range(nrec)) - 1 for f in range(nfld)]
        kmax += [nrec - 1] + fmax
        # knob params (sel + fields); a param menu's Value knob starts with param 0
        params.append(dict(name=m.name, max=nrec - 1, default=0, getstr=f"GetString_Zm{sel + 1}"))
        for f in range(nfld):
            fd = per_rec[0][f]
            params.append(dict(name=fd.name, max=fmax[f], default=fd.default,
                               getstr=f"GetString_Zm{sel + 2 + f}"))
        for r in range(nrec):
            for f in range(nfld):
                fd = per_rec[r][f]
                b = _bits(fd.steps)
                if radix:
                    if fd.steps > 256:
                        raise ValueError(f"{fd.name}: {fd.steps} steps > 256 (radix packing)")
                    product *= fd.steps
                    if product > 1 << capacity:
                        raise ValueError(f"menus need more than Floppy's {capacity} bits "
                                         f"(at {m.name} record {r + 1} {fd.name})")
                    nm = add_names(fd.names) if fd.names else None
                    slot_rows.append((fd.default, fd.steps - 1, nm, b, None))
                    pos += b                    # bit-packed size, for the report
                    continue
                parts, left, vsh, p = [], b, 0, pos
                while left:
                    col = max(i for i in range(9) if starts[i] <= p)
                    fsh = p - starts[col]
                    n = min(left, wid[col] - fsh)
                    parts.append((col, fsh, n, vsh))
                    left -= n; vsh += n; p += n
                    if p > capacity:
                        break
                pos += b
                if pos > capacity:
                    raise ValueError(f"menus need more than Floppy's {capacity} bits "
                                     f"(at {m.name} record {r + 1} {fd.name})")
                if len(parts) > 3:
                    raise ValueError(f"{fd.name}: spans more than 3 fields")
                parts += [(0, 0, 0, 0)] * (3 - len(parts))
                nm = add_names(fd.names) if fd.names else None
                slot_rows.append((fd.default, fd.steps - 1, nm, b, parts))
            if m.is_param:
                macros.append((f"ZM_{_c_name(m.name)}_{_c_name(per_rec[r][0].name)}", slot0 + r))
        if not m.is_param:
            for f in range(nfld):
                macros.append((f"ZM_{_c_name(m.name)}_{_c_name(m.fields[f].name)}(r)",
                               f"({slot0}u + (r) * {nfld}u + {f}u)"))
    for k, mx in enumerate(kmax):
        if mx > KNOB_MAX:
            raise ValueError(f"knob {k + 1}: max {mx} > {KNOB_MAX}")
        if mx >= 1 << WIDTHS["common"][k]:
            raise ValueError(f"knob {k + 1}: max {mx} doesn't fit its {WIDTHS['common'][k]}-bit patch field")

    if radix:
        used = (product - 1).bit_length()
        packed, pos = pos, used
    layout = repr((menu_rows, slot_rows, names, widths) + ((packing,) if radix else ())).encode()
    magic = (0x5A4D0000 | (zlib.crc32(layout) & 0xFFFF)) & 0xFFFFFFFF

    def hx(v: int) -> str:
        return f"0x{v:08X}u"

    L = [
        f"/* Generated by src/common/zmenu/zmenu_gen.py -- do not edit. */",
        f"#ifndef ZMENU_LAYOUT_H",
        f"#define ZMENU_LAYOUT_H",
        f"#ifndef ZM_DESC",
        f"#define ZM_DESC {desc}",
        f"#endif",
        f"#define ZM_NKNOB  {len(kmax)}u",
        f"#define ZM_NMENU  {len(menu_rows)}u",
        f"#define ZM_NSLOT  {len(slot_rows)}u",
        f"#define ZM_NNAMES {len(names)}u",
        f"#define ZM_MAGIC  {hx(magic)}   /* layout id */",
        f"#define ZM_FLOPPY_FXID {FLOPPY_FXID}u",
        f"#define ZM_BITS_USED {pos}u     /* of {capacity} ({widths} widths) */",
        f"#define ZM_RADIX  {int(radix)}u",
        *([f"#define ZM_NLIMB  {(capacity + 15) // 16}u"] if radix else []),
        f'#include "zmenu.h"',
        "",
    ]
    for name, val in macros:
        L.append(f"#define {name} {val}")
    L.append("")
    L.append("static const ZmMenu zm_menus[ZM_NMENU] = {")
    for sel, nfld, nrec, ren, slot0, sn in menu_rows:
        L.append(f"    {{ {sel}u, {nfld}u, {nrec}u, {ren}u, {slot0}u, {sn}u }},")
    L.append("};")
    L.append("static const ZmSlot zm_slots[ZM_NSLOT] = {")
    for d, mx, nm, b, parts in slot_rows:
        nms = 'ZM_NONAMES' if nm is None else str(nm) + 'u'
        if radix:
            inv = struct.unpack("<f", struct.pack("<f", 1.0 / (mx + 1)))[0]
            L.append(f"    {{ {d}u, {mx}u, {nms}, 0u, {inv:.9g}f }},")
        else:
            ps = ", ".join(f"{{ {c}u, {s}u, {n}u, {v}u }}" for c, s, n, v in parts)
            L.append(f"    {{ {d}u, {mx}u, {nms}, {b}u, {{ {ps} }} }},")
    L.append("};")
    if radix:
        L.append("static const ZmCol zm_cols[9] = { " +
                 ", ".join(f"{{ {starts[i]}u, {wid[i]}u }}" for i in range(9)) + " };")
    L.append("static const uint16_t zm_kmax[ZM_NKNOB] = { " + ", ".join(f"{k}u" for k in kmax) + " };")
    L.append("static const char zm_names[ZM_NNAMES * 5 + 1] =")
    if names:
        for i in range(0, len(names), 8):
            chunk = "".join(n.ljust(NAME_CHARS, "\0") for n in names[i:i + 8])
            L.append('    "' + "".join(c if c != "\0" else "\\000" for c in chunk) + '"')
    else:
        L.append('    ""')
    L[-1] += ";"
    L.append("")
    L.append("/* Knob text callbacks (knob numbers 1-based). */")
    k = 0
    for mi, (sel, nfld, *_rest) in enumerate(menu_rows):
        for f in [None] + list(range(nfld)):
            k += 1
            fs = "ZM_SEL" if f is None else f"{f}u"
            L.append(f'ZM_EXPAND_PRAGMA(CODE_SECTION(GetString_Zm{k}, ".text"))')
            L.append(f"int GetString_Zm{k}(unsigned int v, char *s) {{ return zm_text({mi}u, {fs}, v, s); }}")
    L += ["", "#endif /* ZMENU_LAYOUT_H */", ""]
    out_h.write_bytes("\r\n".join(L).encode("ascii"))
    if report:
        how = (f"radix: {math.log2(product):.2f} bits, bit-packed would need {packed}"
               if radix else "bit-packed")
        print(f"[zmenu] {len(menu_rows)} menus, {len(kmax)} knobs, {len(slot_rows)} slots, "
              f"{pos} of {capacity} Floppy bits used ({widths}, {how}), {len(names)} names -> {out_h.name}")
    return [dict(name=p["name"], type="knob", max=p["max"], default=p["default"], scale="enum",
                 audio_min=0.0, audio_max=float(p["max"]), audio_default=float(p["default"]),
                 getstr=p["getstr"]) for p in params]
