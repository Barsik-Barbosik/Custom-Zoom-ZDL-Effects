"""SongBox sound prototypes: bass + keys playing a chord song.

Each instrument set (sets.py: a keys and a bass instrument from
instruments/, picked by the Instr knob) rendered over the same song excerpt with
three rhythm patterns (rhythm 1, 2, 3 = what the Rhy knobs pick).
c_header() writes songbox_sound.h (patterns, note increments); the
instruments' header comes from sets.py.

    python -B proto.py            -> preview/songbox_<set>.wav
    python -B proto.py --set epiano    (a sets.py SETS name)
"""
from __future__ import annotations

import struct
import sys
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from inst_lib import SR, Voice  # noqa: E402
from sets import SETS, c_init, load_set  # noqa: E402

BPM = 100
STEP = int(SR * 60 / BPM / 4 + 0.5)            # samples per 16th

NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def chords(spec: str, rhythm: int) -> list:
    out = []
    for s in spec.split():
        m = s.endswith("m")
        out.append((NAMES.index(s[:-1] if m else s), int(m), rhythm))
    return out


# an example song, shortened: part A on rhythm 1, part B on 2,
# a turnaround on 3, the last bar Am
SONG = (chords("Am Am F G Am Am F G", 0) + chords("Am Am F F Dm Dm G G", 1)
        + chords("Am F Dm G", 2) + chords("Am", 0))

FREQ = [440.0 * 2.0 ** ((m - 69) / 12.0) for m in range(128)]   # a C table

# ------------------------------------------------------------------ patterns
# per rhythm: bass notes (step, len, degree: 0 root, 1 fifth, 2 octave) and
# keys hits (step, len, mode: 0 chord, 1..3 one chord tone (arpeggio)),
# read from patterns.json by song_patterns.py
sys.path.insert(0, str(HERE / "patterns"))
from song_patterns import BASS_APPROACH, BASS_OFS, KEY_CHORDS, KEY_OFS, KEY_ROOTED, KEY_SINGLE, PATTERNS  # noqa: E402


def voicing(root: int, minor: int, code: int = 0) -> list:
    """Keys code's three MIDI notes (KEY_OFS) folded into 55..66 (G3..F#4),
    sorted; codes from KEY_ROOTED: above the root at 48 + root, not folded."""
    if code >= KEY_ROOTED:
        return [48 + root + o for o in KEY_OFS[code][minor]]
    out = []
    for o in KEY_OFS[code][minor]:
        n = 48 + (root + o) % 12
        if n < 55:
            n += 12
        out.append(n)
    return sorted(out)


def bass_note(root: int, minor: int, code: int, next_root: int) -> int:
    """Bass code's MIDI note (BASS_OFS; the approach note from next_root)."""
    r = next_root if code == BASS_APPROACH else root
    return 28 + (r - 4) % 12 + BASS_OFS[code][minor]   # root E1..D#2


# ------------------------------------------------------------------ renderer

def render(name: str) -> list:
    ik, ib = load_set(name)
    return render_with(ik, ib, SONG)


def render_with(ik, ib, song: list, keys_on: bool = True, tail: bool = True) -> list:
    """song (bars) played by keys ik + bass ib (render + LEVEL each);
    keys_on False: bass alone; tail: 1 s ring-out at the end."""
    keys = [Voice() for _ in range(3)]
    bass = Voice()
    out = []
    for bi, bar in enumerate(song):
        root, minor, rh = bar
        pat = PATTERNS[rh]
        next_root = song[(bi + 1) % len(song)][0]
        bev = {s: (ln, d) for s, ln, d in pat["bass"]}
        kev = {s: (ln, m) for s, ln, m in pat["keys"] if keys_on}
        for step in range(16):
            if step in bev:
                ln, d = bev[step]
                bass.trigger(FREQ[bass_note(root, minor, d, next_root)], ln * STEP - STEP // 4)
            if step in kev:
                ln, m = kev[step]
                notes = voicing(root, minor, m)
                if m in KEY_CHORDS:                # chord: all three voices
                    for i, v in enumerate(keys):
                        v.trigger(FREQ[notes[i]], ln * STEP - STEP // 4)
                else:                              # arpeggio: one tone, voices in turn
                    for v in keys:
                        v.gate = 0
                    keys[(step >> 1) % 3].trigger(FREQ[notes[m - 1 if m < 4 else 0]], ln * STEP)
            for _ in range(STEP):
                s = ib.render(bass) * ib.LEVEL
                bass.gate -= 1
                k = 0.0
                for v in keys:
                    k += ik.render(v)
                    v.gate -= 1
                s += k * ik.LEVEL
                s = s if -1.0 < s < 1.0 else (0.99 if s > 0 else -0.99)
                out.append(s)
    for _ in range(SR if tail else 0):             # tail
        s = ib.render(bass) * ib.LEVEL
        for v in keys:
            s += ik.render(v) * ik.LEVEL
        out.append(max(-0.99, min(0.99, s)))
    return out


def note_pool(lanes: list) -> tuple:
    """(pool, offsets): one byte string holding every lane's note bytes,
    shared and overlapped (greedy shortest common superstring: drop lanes
    contained in others, then merge the pair with the longest overlap);
    offsets[i] = where lanes[i] starts in it."""
    uniq = []
    for s in lanes:
        if s and s not in uniq:
            uniq.append(s)
    parts = [s for s in uniq if not any(s != t and s in t for t in uniq)]
    while len(parts) > 1:
        best = (-1, 0, 1)
        for i, a in enumerate(parts):
            for j, b in enumerate(parts):
                if i != j:
                    k = min(len(a), len(b)) - 1
                    while k > 0 and not a.endswith(b[:k]):
                        k -= 1
                    if k > best[0]:
                        best = (k, i, j)
        k, i, j = best
        merged = parts[i] + parts[j][k:]
        parts = [s for t, s in enumerate(parts) if t not in (i, j)] + [merged]
    pool = parts[0] if parts else b""
    return pool, [pool.find(s) if s else 0 for s in lanes]


def c_header(path: Path, swings: list | None = None, bar2: list | None = None) -> None:
    """songbox_sound.h for songbox.c: patterns, phase increments per note of
    octave 0 (doubled per octave by the DSP).

    A pattern's lane (bass, keys) = a 16-bit onset mask (bit = step) in
    ss_pat[4 * p + lane] and one note byte per set bit (len << 4 | degree
    or mode, never 0) in ss_pat_notes from ss_pat[4 * p + 2 + lane] (bits
    0..11; the bass offset's bits 12-15 = the bar2 fill code, `bar2`
    (build.py bar2_codes), the keys offset's bits 14-15 = the swing code of
    the pattern's DrumBox beat, `swings`). Lanes share note bytes: the pool is one
    overlapped string (note_pool). A step's note = notes[ofs + bits of the
    mask below the step] (SS_BITS = popcount)."""
    swings = swings or [0] * len(PATTERNS)
    bar2 = bar2 or [0] * len(PATTERNS)
    nrand = sum(not p["norandom"] for p in PATTERNS)
    L = ["/* Generated by build.py from proto.py -- do not edit. */",
         "#ifndef SONGBOX_SOUND_H", "#define SONGBOX_SOUND_H",
         f"#define SS_NPAT {len(PATTERNS)}u",
         f"#define SS_NRAND {nrand}u                  /* Randm picks one of patterns 0..SS_NRAND - 1 */",
         "#define SS_BITS(m) _dotpu4(_bitc4(m), 0x01010101u)   /* popcount */"]
    masks, lanes, rows = [], [], []
    for pat in PATTERNS:
        for key in ("bass", "keys"):
            row = [0] * 16
            for st, ln, d in pat[key]:
                assert row[st] == 0 and ln << 4 | d, (pat["short"], key, st)
                row[st] = ln << 4 | d
            masks.append(sum(1 << s for s in range(16) if row[s]))
            lanes.append(bytes(b for b in row if b))
            rows.append(row)
    notes, ofs = note_pool(lanes)
    assert len(notes) < 1 << 12 and all(0 <= c < 4 for c in swings) and all(0 <= c < 16 for c in bar2)
    for i in range(len(rows)):                      # decode check (the C lookup)
        m, o = masks[i], ofs[i]
        got = [notes[o + bin(m & ((1 << s) - 1)).count("1")] if m >> s & 1 else 0
               for s in range(16)]
        assert got == rows[i], (PATTERNS[i // 2]["short"], i & 1)
    # tables as initializers of SsConst members (songbox.c: one struct, one base address)
    L.append(f"#define SS_NNOTES {max(1, len(notes))}u   /* {sum(map(len, lanes))} note bytes shared */")
    L += c_init("SS_PAT_INIT", "ss_pat[SS_NPAT * 4u]: bass mask, keys mask, bass ofs | bar2 << 12, keys ofs | swing << 14",
                [f"0x{masks[2 * p]:04X}u, 0x{masks[2 * p + 1]:04X}u, {ofs[2 * p] | bar2[p] << 12}u, "
                 f"{ofs[2 * p + 1] | swings[p] << 14}u,   /* {pat['short']} */"
                 for p, pat in enumerate(PATTERNS)])
    notes = notes or b"\0"
    L += c_init("SS_PAT_NOTES_INIT", "ss_pat_notes[SS_NNOTES]",
                [f"{', '.join(f'0x{b:02X}u' for b in notes[i:i + 12])},"
                 for i in range(0, len(notes), 12)])
    # names: 5 characters x 6 bits in one word (0 end, 1..10 digits, 11..36
    # A..Z, 37..62 a..z; decoded by songbox.c ss_text)
    def name_word(s):
        w = 0
        for i, ch in enumerate(s):
            assert ch.isascii() and ch.isalnum() and len(s) <= 5, s
            c = (1 + ord(ch) - 48 if ch.isdigit() else
                 11 + ord(ch) - 65 if ch.isupper() else 37 + ord(ch) - 97)
            w |= c << 6 * i
        return w
    names = ["Randm"] + [p["short"] for p in PATTERNS]
    L += c_init("SS_PAT_NAMES_INIT", "ss_pat_names[SS_NPAT + 1u]: Randm, the patterns, 6 bits a character",
                [", ".join(f"0x{name_word(s):08X}u" for s in names[i:i + 6]) + ","
                 for i in range(0, len(names), 6)])
    L.append("#define SS_INC_INIT { " + ", ".join(f"{FREQ[n] / SR:.9g}f" for n in range(12)) +
             " }   /* ss_inc[12] */")
    L.append("#define SS_BASS_OFS_INIT { " + ", ".join(f"{o}" for pair in BASS_OFS for o in pair)
             + " }   /* ss_bass_ofs[code * 2 + minor]: semitones above the root */")
    L.append(f"#define SS_BASS_APPROACH {BASS_APPROACH}u   /* bass code: from the next bar's root */")
    L.append("#define SS_KEY_OFS_INIT { " + ", ".join(f"{o}u" for pair in KEY_OFS for tri in pair for o in tri)
             + " }   /* ss_key_ofs[code * 6 + minor * 3 + i] */")
    L.append(f"#define SS_KEY_CHORDS 0x{sum(1 << c for c in KEY_CHORDS):X}u   /* keys codes: all three voices */")
    L.append(f"#define SS_NBASS {len(BASS_OFS)}u")
    L.append(f"#define SS_NKEY {len(KEY_OFS)}u")
    L.append(f"#define SS_KEY_SINGLE {KEY_SINGLE}u   /* key-checked keys codes from here: single tones */")
    L.append(f"#define SS_KEY_ROOTED {KEY_ROOTED}u   /* keys codes from here: above the root (48 + root), unfolded */")
    L.append(f"#define SS_STEP_PER_BPM {4.0 / (60.0 * SR):.9g}f   /* 16ths per sample per BPM */")
    L += ["#endif", ""]
    path.write_bytes("\r\n".join(L).encode("ascii"))


def write_wav(path: Path, samples: list) -> None:
    peak = max(abs(x) for x in samples)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(x * 32767)) for x in samples))
    print(f"  {path.name}: {len(samples) / SR:.1f} s, peak {peak:.2f}")


def main() -> None:
    names = [sys.argv[sys.argv.index("--set") + 1]] if "--set" in sys.argv else list(SETS)
    out = HERE / "preview"
    out.mkdir(exist_ok=True)
    print(f"{len(SONG)} bars at {BPM} BPM: {', '.join(p['name'] for p in PATTERNS)} patterns")
    for n in names:
        write_wav(out / f"songbox_{n}.wav", render(n))


if __name__ == "__main__":
    main()
