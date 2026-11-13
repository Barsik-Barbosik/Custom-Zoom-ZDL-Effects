# ZDL CPU tips

What made DrumBox and SongBox cheaper to run. All numbers were measured on
a G1Xon 1.21 / B1Xon 1.30 with our own load probes (TI cl6x 8.3.15,
`--opt_for_space=3 --disable_push_pop`). Percentages are of the time
between two audio calls.

## 1. The budget

| Fact | Value |
|---|---|
| DSP clock | ~180 MHz (TSCL counter, readable from an effect) |
| One audio call | 8 samples per channel, ~181 µs, **~32,650 cycles** |
| Per sample | ~4,080 cycles for everything in the chain |
| Chain limit (G1on OS) | the declared costs of a patch's effects (name entry +0x28) must sum to ≤ 210 |

The pedal only checks the **declared** cost, not the real one. The
repeat98 linker declares a placeholder of 20 for every effect, so the
pedal allowed DrumBox chains it could not run: crackle and noise with
more effects in the patch. Set `dsp_cost` in `build/linker.py`
from a measurement. The released DrumBox declares 60 (no crackle in a
5-effect chain), SongBox 52. Lower values were tested on the pedal with
stock effects in the chain and crackled, so a cost has to be checked in a
heavy patch before it is lowered.

The stock effects' declared costs don't match their real load either. To
find the real room a patch leaves, put a test effect in your slot that
busy-waits a knob-set share of each call (SongBox `build.py --w1`) and
raise it until the patch crackles. Five heavy patches left 13 %, 20 %,
18 %, 43 % and 52 % of a call, for nearly the same declared room (~50 to
~60). An effect whose mean is ~16 % but whose single calls peak at ~45 %
crackles in the first one however it is optimized: there the declared
cost has to keep it out.

## 2. Measure, don't guess

- **A load probe build.** Read TSCL at the start and end of the audio
  call and keep the peak and a running mean per 4,096 calls. Show the
  numbers on the pedal: DrumBox's probe beeps them at Volume 0 (peak,
  busiest mean, ctx[3] size, CPU MHz); SongBox's shows them as knob names
  ("A 19/6", "V 28/13", "N 12/1": peak / mean % for the call start, the
  voices and the knob names). Footswitch off resets them.
- Meter separate parts of the call, not only the whole: the cost was
  never where we expected it (SongBox's knob-name code had a 19 % peak).
- Build the probe as a separate variant (`build.py --probe`) so the real
  build stays byte-identical. A probe adds code: check the load limit.
- Peaks scatter: something outside the effect (an OS interrupt) takes
  ~12..15 % of a call now and then and lands in whichever part runs.
  Compare means and repeated runs, and for a peak, save the parts of the
  call with the highest whole (one call's breakdown, plus its song
  position): that is what showed SongBox's note-start peak below.
- At `--opt_for_space=3` cl6x writes how it scheduled each loop into the
  assembly listing (`cl6x ... -k --asm_directory=D`): look for "Loop
  source line", "Disqualified", "live too long", "Did not find schedule",
  "Dynlen" and the achieved ii (cycles per iteration).

## 3. Don't do the same work every call

| Trick | Effect | Status |
|---|---|---|
| **Pre-render instead of synthesizing live:** DrumBox renders each lane's drum once into ctx[3] (int16) and plays the cache × velocity | peak 85 % / mean 60 % → playback ~35 % / ~15 % | works |
| **Spread one-off work over calls with a cycle budget:** render until TSCL shows `DB_RENDER_CYC` (8,000 cycles, ~25 %) used, at most 32 samples | kit change: peak ~135 % (crackle) → ~90 %, no crackle | works |
| SongBox's song packer runs in steps (~1,000 cycles per call, counted from the packer's start, not the call's); its wavetables are built a few rows per call (~2,000 cycles) | no load spike on edits or at start | works |
| Work that only a patch change can make necessary (song checksums, slot scans) only after a patch change or a gap between calls, not every call | ~1,000 cycles per call for a 128-bar song | works |
| On an edit, only the steps it needs: the Floppy poll and slot scans only on the calls that load or verify the song, not on every packer step | fewer clicks while a knob turns | works |
| Cache what can't change while running: the firmware fingerprint check once (with an inverted copy to notice an overwritten state), slot marks read once per call | call start A 19/9 → 18/6 | works |
| **Wait for the knob to settle before the work:** DrumBox renders a kit only after the Kit knob has stayed put for 800 calls (~145 ms), so turning through kits costs no render time. SongBox switches its Instr kit the same way | kit-turn render stops; no start spike; SongBox: the crackle while turning Instr gone | works |
| **One start per call:** a lane's first sample is its own call, and the render loop stops after each lane's sample | no spike at the start of a drum | works (not measured) |
| Knob names: write one name per call, in turn, and only bytes that differ | names N peak 19 → 12 % | works |
| Rewrite knob names only when what they show changes (plus a slow refresh) | N mean 7 → 1 % | works |
| A check pass over a big cache in slices (`DB_CHECK_N` samples per call), fast only right after a patch switch | | works |
| Skip work that has no audible result: muted lanes, empty lanes, the bands a drum doesn't use | | works (but see section 7) |

A debug probe that scanned 256 RAM words plus a whole watch list in one
call (~33,000 cycles) crackled on its own: spread long loops over calls.

## 4. Fast inner loops at `--opt_for_space=3`

cl6x at `-ms3` overlaps loop iterations (software pipelining) **only
through SPLOOP**, and only when:

- the chain carried from one iteration to the next is short (~8 cycles
  worked, ~16 was refused: "loop carried dependency bound too large");
- one iteration's dynamic length is ≤ 48 cycles ("Dynlen of 53 > 48");
- no value lives longer than one iteration interval ("live too long").

Without pipelining a loop runs each sample end to end (the rock organ's
table loop: ~159 cycles per sample unpipelined, ~36 pipelined).
What made SongBox's instrument loops pipeline:

| Trick | Why |
|---|---|
| **Process a voice per run of samples** (`ss_run`: the 8-sample piece between two sequencer events), not one sample of every voice per loop | the per-sample work becomes a plain loop; voice fields stay in registers |
| Copy the voice into a local struct before the loop and back after | registers instead of a load and store of every field per sample |
| No `?:` / `if` in the loop: cl6x at -ms3 turned every `?:` into a branch | branch-free code pipelines |
| **Integer phases:** `uint32`, 2^32 = one cycle, wrap for free | no float compare-and-subtract in the carried chain |
| Envelopes as one multiply-add: `env = env * (rel + k * (1 - att - rel)) + k * att` (k = gate) | one short carried chain instead of an attack / release branch |
| **Split the sample into short loops** through a small scratch buffer: (a) carried state → phases and levels, (b) the table reads, (c) clip, DC blocker, gain | each loop short enough for SPLOOP (ii 9 / 11 / 9) |
| Work that changes slowly once per piece, not per sample (tremolo / LFO gain) | took the epiano loop under the 48-cycle limit |
| Cheap identities: clip `c = \|x+1\| − \|x−1\|`, DC blocker output `(y − lp_old) × gain`, levels pre-multiplied, a shallow sum tree | shorter chains, fewer operations |
| **Wavetables in ctx[3]** built at start from harmonic lists: one table read replaces a sum of sines | rock organ 213 → 159 cycles per sample (table alone) → ~36 with the loops above |

Measured (SongBox voices, peak / mean %):

| Step | Rock organ | Electric piano |
|---|---|---|
| Old player: one sample of every voice per loop, branches | 51 / 38 | 54 / 44 |
| Voice runs, local copy, branch-free | 39 / 25 | 31 / 18 |
| Wavetable + three short pipelined loops | 28 / 13 | 33 / 17 |
| Tremolo once per piece, cheaper clip | | 24 / 10 |

Rock organ per sample: ~213 cycles → ~36 cycles.

## 5. Things that cost size for speed

- Inlining three chord voices into one loop would have cut call overhead
  but cost +2.6 KB for one instrument: over the load limit. Size and
  speed pull against each other at the limits; measure both.
- Rarely-run code moved out of the audio function (to save size) costs
  only a call per use; keep anything per-sample inline.
- SongBox's song packer with its small helpers called instead of inlined:
  ~1,050 B less, but the longest packer step +4 % of a call on the
  worst-case song (128 bars of one chord): the helper in its copy loop
  became ~240 calls. That one stays inline (~770 B less in all).
- Two small caches cost ~150 B and were kept: the start of each run of
  bars with the same rhythm (the random-rhythm lookup walked back over
  the run on every event), and the last value written to each DrumBox
  knob (no rewrite of an unchanged one).

## 6. ctx[3] and patch switches

- ctx[3] (~704 KB per slot) is fast enough for caches and tables: the
  ZDL is copied to RAM at load and `.const` sits in the same memory.
  Only big tables read at random every sample would be slow, from either.
- The pedal lends ctx[3] to other patches' effects: check a cache before
  trusting it (checksum in slices; a TSCL gap > ~50 ms between calls
  means a patch switch) and rebuild it in budgeted steps.

## 7. Keep the cache warm for rare work

- SongBox skipped voices once they had faded out. While they were silent,
  the patch's other effects pushed their wavetable, records and code out
  of the cache, so the call where the next chord started read all of it
  from memory: **the voices cost 35..40 % in that one call (mean 5 %)**,
  and the whole call reached ~60 %.
- Fix: run a faded voice on silently (skip only a part that isn't in the
  kit). Worst call 64 % -> 45 %, the chords-only kit ~60 % -> ~38 %, the
  means unchanged, and 32 B less code.
- Touching a few lines of the tables per call (a manual prefetch) helped
  only half as much (worst call 54 %) and cost ~130 B of code: it can't
  keep the code or every line warm.
- So a skip saves mean CPU but can move the cost into a cold peak. Measure
  the peak call before and after.
- The same holds for code that runs rarely. SongBox's note-event function
  ran once per 16th and was cold every time: **5..7 % of a call at least
  (mean 9..11 %) for a few hundred cycles of work**, always in the call
  where the notes start.
- Fix: a look-ahead. Mid-step, in a call where nothing starts, the next
  step's notes are worked out into a small buffer with a key (bar, step,
  pass, song checksum). The note-start call checks the key and copies the
  buffer into the voices (a key mismatch works them out there, so a wrong
  guess costs time, not notes). Note start 9..11 % -> 1 %; the cold cost
  moved to the mid-step call. It cost ~450 B of code.
- Keep the note-start part small and out of line: inlined into the audio
  function the copy loop cost 384 B there, as a cold helper ~280 B.
