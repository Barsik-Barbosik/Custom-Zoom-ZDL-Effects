"""Saw (keys record + its wavetable; Instr Saw / Saw+B / Saw+T, its table
also under Plk = pluck:saw): the table's low half at the note plus its
high half on a free phase, the two 0.3 % apart (+-5 cents, a slow beat: two
detuned oscillators), driven a little into the soft clip (peak in ~0.5: ~8 %
squash), 5 ms attack, 150 ms release as tri.py. Out x 1 / (1.5 x drive) / 2
(two reads). Also plays its kit's bass, unchanged; as its own bass (Instr
Saw) at BASS_LEVEL, about the triangle bass's peak (tri.py). Table: low half
a band-limited saw (harmonics 1..20, 1/n), high half a softer one (1..12)
for the detuned second read; both scaled to a peak of 1 (the record's gains
are per unit of the table's peak, so other records play it unchanged)."""

from inst_lib import coef, rise
from engine import TWO32, _peak

ROLE = "keys"
LEVEL = 0.1
BASS_LEVEL = 0.18
GAIN = 1.25             # as another record's table (Plk: +2 dB, it sounded quiet)
_PRE = 0.25             # drive: a read's peak into the clip
_D = 0.003              # detune: second read / first
REC = dict(pa=(1.0 - _D / 2) * TWO32, pc=(1.0 + _D / 2) * TWO32, r0=1, m2=0,
           att=rise(5), rel=coef(150),
           g0=_PRE, r2=1.0, post=1.0 / (2.0 * 1.5 * _PRE))
_L = [(n, 1.0 / n, "sin") for n in range(1, 21)]
_H = [(n, 1.0 / n, "sin") for n in range(1, 13)]
LOW = [(h, a / _peak(_L), k) for h, a, k in _L]
HIGH = [(h, a / _peak(_H), k) for h, a, k in _H]
