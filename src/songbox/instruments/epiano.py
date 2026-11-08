"""EPiano (keys), shimmering Wurlitzer-style reed: two reeds a few cents apart
(slow beating) + a little 2nd harmonic into an asymmetric cubic soft clip
(harder at the strike), DC blocked, fast tremolo (6.5 Hz, 30 %). Also plays
its set's bass, unchanged. Engine: engine.py / engine.c. Table: low half reed 1
+ its harmonics 2-4, high half reed 2, read at its own free-running phase
ip2 (1.003 x). Every envelope moves smoothly (no jump at a new note, even
over a ringing one): the level env rises (att) to the slow decay lp2, the
strike menv rises to bp, which decays."""

from inst_lib import SR, coef, lp_coef, rise
from engine import TWO32

ROLE = "keys"
LEVEL = 0.18
LOW = [(1, 0.6, "sin"), (2, 0.3, "cos"), (3, 0.14, "sin"), (4, 0.07, "cos")]   # reed 1 + harmonics 2-4
HIGH = [(1, 1.0, "sin")]                        # reed 2
_ATT, _DC = rise(15), lp_coef(20.0)
REC = dict(pc=1.003 * TWO32, r0=1, m2=0,        # reed 2: own phase, a few cents up
           att=_ATT, dec=coef(3500), rel=coef(140),
           bdec=coef(400), ma=1.0 - _ATT, mb=_ATT, bp0=1.0, mk=1.0,   # strike
           g0=0.55, g1=0.9, r2=0.6,             # level x (0.55 + 0.9 strike); reed 2 at 0.6
           e=0.0375, la=1.0 - _DC, dc=_DC,      # even harmonic in the clip; DC blocker
           rot=int(6.5 / SR * TWO32), t0=0.85, ts=-0.15, post=1.0 - _DC)   # tremolo
