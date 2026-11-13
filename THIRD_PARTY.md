# Third-party material

This repository's own code is MIT (see `LICENSE`). It also contains:

| What | From | Licence |
|---|---|---|
| `build/linker.py` (modified, see its header), `build/screen_image.py`, `build/manifest_params.py` | [repeat98/ZoomMultistompZDL](https://github.com/repeat98/ZoomMultistompZDL) | MIT (repository code) |
| The materializing `_init` in `build/linker.py`, `build/tests/test_init_materialize.py` | [themanro/ZoomMultistompZDL](https://github.com/themanro/ZoomMultistompZDL) | MIT, Copyright (c) 2026 Roman Rusinov |
| The 40-byte `Dll_` in `build/linker.py` (`_COMPACT_DLL_WORDS`) and the shape of `build/own_handlers.asm` | Leemuzhko's `dll.asm` and `zui.asm` in [repeat98/ZoomMultistompZDL](https://github.com/repeat98/ZoomMultistompZDL) (`build/runtime/`) | MIT (see its `build/runtime/LICENSE` for scope) |
| The call stub in `build/own_handlers.asm` | TI C6000 code generation tools, run-time library source `lib/src/call_stub.asm` | BSD-3-Clause, Copyright (c) 2000 Texas Instruments Incorporated (notice in the file) |
| Sampled DrumBox drums (`src/drumbox/drums/*.json`, resynthesized from WAV samples; the WAVs are not included) | KB6 Samples Archive © 1996 by Kai | [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/) |


Zoom, Zoom Effect Manager and the pedal names are trademarks of Zoom
Corporation. This project is not affiliated with or endorsed by Zoom.
