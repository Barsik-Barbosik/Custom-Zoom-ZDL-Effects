"""Rock organ (keys): most drawbars out (16', 8', 5 1/3', 4', 2 2/3', 2',
1'), key click (harmonic 32), driven into a soft clip, fast rotary (6.7 Hz
triangle: pitch +-0.3 % + level). Also plays its set's bass, unchanged.
Engine: engine.py / engine.c. Table: low half the drawbar sum, high half the
click, read at the same phase (ip2 = ip)."""

from inst_lib import SR, coef, rise
from engine import TWO32

ROLE = "keys"
LEVEL = 0.126 / 1.19    # without sets.MASTER's +10 % and +8 % (the others have them)
LOW = [(1, 0.8, "sin"), (2, 1.0, "cos"), (3, 0.8, "sin"), (4, 0.7, "cos"),
       (6, 0.5, "cos"), (8, 0.5, "cos"), (16, 0.4, "cos")]
HIGH = [(32, 1.0, "cos")]
_D = 0.003              # rotary pitch depth
REC = dict(pa=(1.0 - _D) * TWO32, pb=8.0 * _D, pc=(1.0 - _D) * TWO32, r0=0, m2=1,
           rot=int(6.7 / SR * TWO32), t0=1.0, tt=-0.2,   # rotary level 1 .. 0.8
           att=rise(4), rel=coef(60),
           bdec=coef(4), bp0=0.3, b2=1.0,        # click
           g0=0.45)
