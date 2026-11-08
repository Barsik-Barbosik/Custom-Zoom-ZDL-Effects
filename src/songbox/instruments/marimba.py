"""Marimba (keys; Instr Mrm+BG, Mrm+BT, ...): warm and woody, an octave above
the note: a sine + the bar's tuned 4th partial (3.93 x, HIGH on its own
phase ip2 at pc) as a short knock (~60 ms, b2 bp), soft 1 ms mallet, short
decay. The table (two sines, peak 1) also serves kalimba.py (kalimba:marimba)."""

from inst_lib import coef, rise
from engine import TWO32

ROLE = "keys"
LEVEL = 0.1120                  # 30 % of keys-alone RMS = EPiano's, then + 20 %
LOW = [(1, 1.0, "sin")]
HIGH = [(1, 1.0, "sin")]
_PRE = 1.0 / 64.0
REC = dict(pa=2.0 * TWO32, pc=2.0 * 3.93 * TWO32, att=rise(1), dec=coef(500), rel=coef(300),
           bp0=1.0, bdec=coef(60), b2=0.6, g0=_PRE, post=1.0 / (1.5 * _PRE))
