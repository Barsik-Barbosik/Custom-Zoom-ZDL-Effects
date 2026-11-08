"""Pluck (keys record only; on the saw's table = Plk): the plain gated
record of tri.py with a fast decay while the note is held (-60 dB after
400 ms: a pluck), 2 ms attack, 50 ms release. Also plays its kit's bass,
unchanged."""

from inst_lib import coef, rise

ROLE = "keys"
LEVEL = 0.1 * 1.25 * 0.65   # +2 dB: the pluck decays, so it sounded quieter;
                            # x 0.65: set on the square table it first played
_PRE = 1.0 / 64.0
REC = dict(att=rise(2), dec=coef(400), rel=coef(50),
           g0=_PRE, post=1.0 / (1.5 * _PRE))
