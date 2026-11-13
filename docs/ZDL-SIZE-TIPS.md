# ZDL size tips

What made DrumBox, Floppy and SongBox smaller. All numbers were measured
on our own builds (TI cl6x 8.3.15 and this repository's
`build/linker.py`). "Works" means the build loaded and played on a B1Xon
1.30 or G1Xon 1.21; everything else was only built. Sizes are file bytes
unless marked alloc.

## 1. Know the two limits

| Limit | What counts | Value |
|---|---|---|
| Flash | the whole `.ZDL` file | 4,090-byte blocks (8 blocks = 32,720 B) |
| Load (G1on family) | alloc = `.text` + `.const` + `.fardata` | 30,104 B loaded, 30,136 B froze on selecting the effect |

The gap between the two is ELF bookkeeping: `.rela.dyn`, `.dynsym`,
`.dynstr`, `.hash` and the headers. In SongBox it was 4,446 B at the
start. Tricks that cut only the bookkeeping help flash, not alloc. Code
and data count in both. Moving data between `.const` and `.text` does
nothing for alloc, because alloc is their sum.

## 2. Measure first

- Read the linker log's `.text layout` lines before changing C. The
  linker's own parts (knob handlers, their copies, float divide, `_init`)
  were bigger than any C change more than once. When a DrumBox kit was
  250 B over the load limit, about 4 KB of linker parts were waste.
- To see size by source line, map `.audio` / `.text` addresses to lines
  through the object file's `.debug_line`. Helpers that are inlined show
  up at every call site.
- To count relocations by symbol: `ofd6x -v --obj_display=none,relocs
  x.obj`. In the ZDL, each distinct relocation target address gets its own
  `.dynsym` entry.
- cl6x code grows in 32-byte fetch packets and jumps unpredictably: the
  value of one constant once moved DrumBox's audio code by about 1 KB.
  Compare against a byte-exact rebuild of the old source, and don't read a
  32-byte jump as the effect of your change. Change one thing at a time
  and keep what shrinks the file.
- Make the feature work first; size work is a separate step.

## 3. Compiler flags

- `--opt_for_space=3` together with `--disable_push_pop`. Without the
  second flag cl6x calls `__c6xabi_push_rts` / `pop_rts`, which the Zoom
  runtime doesn't have. DrumBox code went from 24.4 KB to 16 KB at
  `--opt_for_space=1`. Works.
- At `--opt_for_space=3` cl6x overlaps loop iterations only through
  SPLOOP: short chains carried from one iteration to the next, at most 48
  cycles per iteration, nothing kept longer than one iteration interval.
  Short, split loops are good for both size and speed.
- After every build: no `DINT` / `RINT` in the `dis6x` output, only the
  descriptor undefined in `nm6x`, and `.fardata` as small as expected.

## 4. Code

| Trick | Saving | Status |
|---|---|---|
| **Rarely-run code out of line:** `FUNC_CANNOT_INLINE` + `CODE_SECTION(".text")` + `FUNC_INTERRUPT_THRESHOLD 1`, called from the audio function | SongBox −1,264 B; its audio code 21,120 → 9,408 B | works |
| Small helpers inlined at many call sites out of line too: SongBox's song packer (`sp_put`, `sp_get_bits3`, `sp_append`, `sp_next_addr`, `sp_mod12`; one more in its copy loop stays inline) and a text routine | ~−770 B alloc | works (B1Xon 1.30). The helper in the copy loop out of line too: −290 B more, but the longest packer step +4 % of a call on the worst-case song, so it stays inline |
| Knob texts: one `static` getstr body in `.text`, tiny `GetString_*` wrappers that call it (a call inside `.text` needs no relocation) | menu library 3,104 → 704 B; DrumBox 672 → 416 B | works |
| One generic engine driven by data records instead of code per instrument | SongBox −368 B file, −560 alloc; a new instrument ~120 B of data instead of ~900 B of code | works |
| Pick a table index in each branch and copy the text once at the end, instead of a copy in every branch | a copy per return got inlined: +632 B and +24 relocations | works |
| Send every text (words, instrument names, chord names with a prefix and a minor "m") through that one copy: branches only pick a string number | SongBox −224 B of code (three inlined copy loops → one) | works |
| Byte copies (bar lists) through one small out-of-line copy routine instead of a copy loop at each site (cl6x software-pipelines each inlined loop, ~60 B apiece) | SongBox −128 B of code (four copy loops → one) | works (B1Xon 1.30) |
| Look up a per-firmware table row once per call and keep the pointer, instead of indexing the table at every use | −352 B code, −16 relocations | works |
| Formulas instead of compare chains or macros expanded per entry | ~7 B per entry per expansion | works |
| Branch-free operations (`x \|= (a - b) >> 31`), small maps as nibble lists, offset counters | whole 32-byte packets | works |
| A new case through the existing code with its constants in variables, instead of a separate block after it (SongBox's unfolded chord voicings: the note routine's fold limits 12 / 55 become 99 / 0) | the separate block cost +128 B of code, the variables nothing | works |
| Firmware math (`ctx[49]` sine, `ctx[40]` exp, `ctx[46]` power, `ctx[39]` divide, see HARDWARE-NOTES) instead of own approximations, tables or the bundled divide | a call instead of inlined code / table bytes | functions work (B1Xon 1.30) from code built without `-ms`. SongBox: sine + power instead of an own sine and pitch loops = 224 B less, but at `-ms` cl6x calls them through `__c6xabi_call_stub`, and those builds froze the pedal on select (3 builds). **Not used** |

Notes:
- Moving code out of line gains most for small helpers inlined at many
  sites. A block with a single call site gains little, and some moves grow
  the file. Keep per-sample loops inline.
- The linker enters the audio function at the start of `.audio`. Put no
  callable helpers in `.audio`: inline them, or place them in `.text`.
- Calls from `.audio` into `.text` are PCR_S21 relocations. Check that
  every call lands on its function in the disassembled ZDL: an early
  version of this linker missed by the call's offset in `.audio`.

## 5. Data

| Trick | Saving | Status |
|---|---|---|
| Tables as `static const` in `.const` instead of code that stores immediates (the linker adds a `.rela.dyn` entry per address load, as Zoom's effects do) | a table word 1–4 B against ~11 B per immediate store | works |
| Palettes: each distinct value once, plus one index byte per entry | DrumBox 8 → 6 flash blocks (with the item above) | works |
| Fixed-width names (5 bytes, padded with NUL) | | works |
| Sparse lanes: a 16-bit onset mask per lane plus one byte per note, indexed by a popcount (`_dotpu4(_bitc4(m), 0x01010101)`) | SongBox ~40 → ~23 B per rhythm | works |
| Share note bytes: an offset per lane and all lanes' notes as one overlapped string (the generator merges equal and overlapping runs) | SongBox's 29 rhythms 425 → 361 B (notes 251 → 129 B); copied lines cost nothing | works |
| **All tables in one struct** (the generators write initializers; the old table names stay as macros, so the code is unchanged) | SongBox −320 B; −432 B with `rela_const_base` below | works |
| Wavetables built at run time from harmonic lists, a few rows per call, into the effect's large buffer (`ctx[3]`) | tables cost RAM, not flash | works |
| The table struct's address kept in the effect's state (set once per call; code reads it from there): one load from memory instead of an MVKL / MVKH pair with two `.rela.dyn` entries per use. Helpers without the state get it as an argument | SongBox −320 B file (17 → 1 address loads), +64 B of code | works |
| Names as 5 × 6 bits in one word (0 end, digits, A–Z, a–z), decoded by the one text routine | SongBox −72 B of table for 70 names, +32 B of code | works |
| Per-OS address rows without the fields that are the same on every OS (the id test as a constant), small counts as bytes | zmenu rows 560 → 512 B, −128 B of code | works |
| All texts (knob names, knob texts, chord and instrument names) as one pool of NUL-terminated strings instead of fixed 8-byte slots; one shared out-of-line routine skips to string *i* | SongBox texts 526 → 360 B, −232 B file in all (the 8-byte compare loop went too) | works |
| Rare per-entry values in spare bits of another table: SongBox's "every second bar" fill, used by 6 of 149 rhythms, as a 4-bit code in the unused top bits of a note offset (already masked off when read) into a 5-entry list, instead of a byte per rhythm | −112 B alloc (−144 B table, +32 B code) | works |
| A value that mostly follows the entry's index, stored as a code into a short list of differences: SongBox's DrumBox beat per rhythm (beat = rhythm number + one of 8 differences), the 4-bit code in spare bits of a note offset and of the name word, instead of a byte per rhythm | −96 B alloc (149 rhythms) | works (B1Xon 1.30) |
| Drop wavetable harmonics whose amplitude is exactly 0 (a plucked string's nodes) | 4 B per term | works |
| Instrument records (29 words each) as one index byte per field into a pool of the distinct words, copied into the effect's state when the wavetables are built | SongBox 6 records 696 → 418 B (+64 B of code) | works (B1Xon 1.30) |
| One record (envelope, gains) shared by several wavetables: a set names record and table separately | a new wave costs only its harmonic list | works (B1Xon 1.30) |
| Per-OS fingerprint: the words that are the same on every OS stored once, a run of consecutive addresses as one base address, the values as one array; the check out of line (it runs once after a reset), without RAM / alignment tests on the table's own constant addresses | zmenu rows 512 → 412 B; the check 192 B inline in the audio code → 160 B in `.text`. An unrolled first try cost +256 B of code | works (B1Xon 1.30) |
| Levels as bytes (level × 4096, ≤ 0.6 % off) instead of floats | SongBox 64 → 16 B | works (B1Xon 1.30) |
| Note steps from one base step and a semitone factor (a loop in the rarely-run trigger) instead of a 12-float table | −48 B table, +24 B code | works (B1Xon 1.30) |
| Small byte tables in a byte struct instead of `uint8_t` arrays: cl6x gives each array its own 8-byte-aligned section, a struct of bytes only 1-byte alignment | SongBox's 4 small tables: 13 → 5 B of padding | works (B1Xon 1.30) |

Notes:
- A palette only pays when values repeat. SongBox's rhythm names and
  drum settings were all different, so a palette would gain nothing there.
- Names packed into 6 bits per character cost +224 B in DrumBox (the
  decoder was bigger than the saving). It saves 1 B per name, so it only
  pays with many names (break-even around 80).
- Packing text into 7 bits per character would save only 1/8 of the
  table; the decoder eats most of it. Dropping the padding of fixed-width
  slots saved far more (SongBox: 7-bit −66 B of table, pool −166 B).
- Don't expect a struct or a local pointer to make cl6x load a table's
  address only once. It reloads the address (MVKL / MVKH) at each use and
  moves the load into each branch, even from a local pointer. The struct
  still pays through the linker (below).

## 6. Linker options (`build/linker.py`)

Always on (no code from Zoom's effects in a built ZDL):

| What | Saving | Status |
|---|---|---|
| Own knob handlers (`build/own_handlers.asm`): an OnOff handler, one shared knob body and TI's call stub, plus a 16-B entry stub per knob (knob id and parameter offset in registers) | DrumBox 800 B against copied stock handlers; SongBox 32 B against the trimmed stock block | works |
| The shared body takes a knob max over 150's range law for every knob. Every law gives raw = value / 100, and the 200-step law doesn't clamp past 200 (a max-210 Tempo knob reaches 250 BPM) | | works |
| The 40-B `Dll_` (Leemuzhko's `dll.asm`) instead of a 200-B copy of a stock one | 160 B | works |
| No float-divide helper and no `.c6xabi.attributes` section | 640 B, 123 B | works |

Options (off by default):

| Option | What it does | Saving | Status |
|---|---|---|---|
| `init_call_loop` | `_init` calls the knob entry stubs from one 160-B loop, not a 64-B block each | 288 B (DrumBox) | works |
| `drop_ti_abs_symbols` | no `__TI_*` bookkeeping symbols | ~170 B with the next line | works |
| `dynamic_entries=17` | `.dynamic` without its padding entries | | works |
| `rela_const_base` | the address-load relocations into `.const` share one `.dynsym` entry, with the offset in each entry's addend. 82 of 265 stock effects we scanned use such addends | 120 B; with one table struct, all 17 table targets share one symbol | works |
| `anon_edit_syms` | the knob handlers' `.dynsym` entries without names, as the knob-text callbacks' already are (Zoom's effects name them `<audio>_<knob>_edit`) | SongBox 231 B of `.dynstr` | works |
| no `_init` | for an effect that never reads its knob values from `params[]` | | works |
| `rela_text_base` | the descriptor's knob-text callback pointers share one `.dynsym` entry, the offset in each entry's addend (no Zoom effect does this for these pointers) | SongBox 192 B | works (B1Xon 1.30) |
| `drop_empty_shdrs` | no section headers for the empty `.rela.plt` and `.audio` | 97 B | works (B1Xon 1.30) |
| `overlap_zero_tail` | the linker's zero-filled coefficient table (68 B) and an all-zero knob graphic go into the 128 zero bytes at the end of the 212-B image info block (its unused knob slots) instead of after it; every byte the pedal reads stays the same | SongBox 80 B alloc | works (B1Xon 1.30) |
| `anon_abi_syms` | the local `.dynsym` entries (picture, descriptor, audio / init functions...) without names; `Dll_<name>` and the library name stay | SongBox 180 B of `.dynstr` | works (B1Xon 1.30) |

Each `.dynsym` entry costs 16 B plus 8 B of `.hash` once there are more
than 17. Each MVKL / MVKH address load costs 8 B of code plus 24 B of
`.rela.dyn`.

## 7. Screen picture

The 128×64 picture is run-length coded over 8-pixel column bytes, in 8
bands of 8 rows. A byte equal to its left neighbour costs almost nothing;
every change costs 1 B. Plain areas, horizontal edges on 8-row
boundaries, and fewer small details make it smaller. Moving text a few
pixels changes the size. SongBox's picture takes 840 of 1,024 raw bytes,
with the busy middle bands at about 1 B per column.

## 8. Tried, didn't help

- ELynx's strip shrinker: our files have no debug sections to strip.
- Small C cuts while the linker parts are the real cost.
- Moving data between `.const` and `.text`.
- A local base pointer for tables (cl6x reloads the address anyway).
- Copying a header library's source into the effect: `static inline`
  header code is already compiled into the effect.
- Generating saw / square / triangle wavetables by a formula (1 / n by a
  reciprocal estimate) instead of harmonic lists: the code (~190 B) cost
  as much as the 176 B of harmonics it replaced. A second try (46
  harmonics -> 4 series entries, 168 B less data) grew the loaded size by
  24-248 B: float latency fills the loop with NOPs, so a stored 4-byte
  harmonic is cheaper than computing it.
- Instrument records as a default record + a bitmask per record of the
  fields that differ (only those indexes stored): −8 B for 10 records, as
  the decode code grew almost as much as the data shrank.
- Most linker options (above) shrink the file, not the code + data the
  pedal loads, so they don't help against the load limit
  (`overlap_zero_tail` is the exception: it shrinks `.const`).

## 9. SongBox, step by step

| Step | File (B) | Alloc (B) |
|---|---|---|
| Start (0.7 beta), 9 blocks | 33,722 | 29,276 |
| Linker: `init_call_loop`, `drop_ti_abs_symbols`, `dynamic_entries=17` | 33,230 | 29,012 |
| Sparse rhythm lanes | 32,862 | 28,644 |
| Per-firmware row pointer once per call | 32,294 | 28,292 |
| State end check (a fix, not a saving) | 32,358 | 28,356 |
| Rarely-run code out of line | 31,094 | 27,140 |
| One instrument engine | 30,726 | 26,580 |
| A smaller STATUS text change | 30,662 | 26,516 |
| All tables in one struct (with the base address passed through `_mvd`, so every load used the same symbol: −152 B, but +160 B of code) | 30,190 | 26,692 |
| `rela_const_base`, `drop_attributes` | 29,947 | 26,692 |
| The same without `_mvd` (`rela_const_base` folds the offsets) | 29,835 | 26,532 |
| `compact_edit_stubs` with the 200-step law | 29,099 | 25,796 |

In total −4,623 B of file (9 → 8 blocks) and −3,480 B of alloc. The room
then went to 120 more rhythms and features (31,491 → 32,635 B with 149
rhythms). A precision bass (one instrument, 196 B of data) and two more
instrument sets took alloc 244 B over the load limit; the two rows above
(spare bits, one text copy) and the zero harmonics brought it back to
29,860 B of alloc (32,571 B file) with all 149 rhythms.
