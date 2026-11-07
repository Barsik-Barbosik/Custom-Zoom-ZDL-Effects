"""Jazz organ (keys): drawbars 888000000 (16', 5 1/3', 8'), 2 2/3'
percussion, key click, no vibrato. Also plays its set's bass, unchanged.
Engine: engine.py / engine.c. Table: low half the drawbar sum (harmonics 1, 2,
3 of the 16'), high half harmonic 2; the percussion (harmonic 6) is the
high half read at ip2 = 3 ip, the click (harmonic 32) at ip << 4. No clip
wanted: the signal goes in at 1/64 (the clip is linear there) and comes out
x 64 / 1.5."""

from inst_lib import coef, rise
from engine import TWO32

ROLE = "keys"
LEVEL = 0.063
LOW = [(1, 1.0, "sin"), (2, 1.0, "cos"), (3, 1.0, "sin")]
HIGH = [(2, 1.0, "cos")]
_PRE = 1.0 / 64.0
REC = dict(pc=3.0 * TWO32, r0=0, m2=3, sh=4,    # ip2 = 3 ip (percussion), click at 16 ip
           att=rise(4), rel=coef(60),
           ma=coef(250), mk=0.0, m0=0.8, q2=1.0,    # percussion: 0.8, decays
           bdec=coef(4), bp0=0.25, b3=1.0,       # click: 0.25, 4 ms
           g0=_PRE, post=1.0 / (1.5 * _PRE))
