"""Triangle synth (keys record + its wavetable; as a kit's bass: the +T
kits and BassT): a plain gated wave, 5 ms attack, 150 ms release, no clip
colour (in at 1/64, out x 64 / 1.5), no second read. Band-limited
triangle: odd harmonics 1..13, 1/n^2 with alternating sign (sin terms),
scaled to a peak of 1 (the record's gains are per unit of the table's
peak). As bass at BASS_LEVEL: the precision bass's peak (~0.6 x 0.35), a
little less as it doesn't decay."""

from inst_lib import coef, rise
from engine import _peak

ROLE = "keys"
LEVEL = 0.1
BASS_LEVEL = 0.18
_PRE = 1.0 / 64.0
REC = dict(att=rise(5), rel=coef(150),
           g0=_PRE, post=1.0 / (1.5 * _PRE))
_T = [(n, (-1.0 if n % 4 == 3 else 1.0) / (n * n), "sin") for n in range(1, 14, 2)]
LOW = [(h, a / _peak(_T), k) for h, a, k in _T]
