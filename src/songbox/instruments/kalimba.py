"""Kalimba (keys record on marimba.py's sine table; Instr Klm+BG,
Klm+T, ...): bright and plucky, an octave above the note: a sine + a strong
5.4 x metal partial that dies in ~30 ms (b2 bp) + a tick at 4 x the sine
(HIGH at ip << 2, b3 bp), hard 0.3 ms attack, long ring."""

from inst_lib import coef, rise
from engine import TWO32

ROLE = "keys"
LEVEL = 0.0766                  # 30 % of keys-alone RMS = EPiano's, then + 15 %
_PRE = 1.0 / 64.0
REC = dict(pa=2.0 * TWO32, pc=2.0 * 5.4 * TWO32, att=rise(0.3), dec=coef(1800), rel=coef(700),
           bp0=1.0, bdec=coef(30), b2=0.9, b3=0.3, sh=2,
           g0=_PRE, post=1.0 / (1.5 * _PRE * 1.3))
