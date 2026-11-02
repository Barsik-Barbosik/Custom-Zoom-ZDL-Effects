;* init_call_loop.asm -- one loop block of the materializing _init that calls
;* a run of knob edit handlers lying at a fixed stride (the compact LineSel
;* clones of knobs 3..9, 96 B apart), instead of one 64-byte call block each.
;*
;* Inserted by build/linker.py (LinkerConfig.init_call_loop) after the setup
;* block and the single call blocks, before the epilogue of themanro's
;* init_materialize.asm. Same rules as its call block: ctx is reloaded from
;* the frame (*+B15[2]) before every call, the handler is skipped when
;* ctx[31] is null, the call is a register branch with an ADDKPC return,
;* nothing is kept in registers across a call (a handler tail-calls firmware
;* code that need not keep our registers). The next handler address lives in
;* the frame's free word *+B15[3] (the prologue's ctx push writes only the
;* low word of its doubleword slot; callees write below B15).
;*
;* All addresses come from ADDKPC (PC-relative, no relocation): the block is
;* copied as a unit to a 32-byte aligned address. The linker patches the
;* three ADDK constants (placeholders 0x1111 / 0x2222 / 0x3333):
;*   FIRST  first handler - block start
;*   STRIDE handler stride
;*   END    first handler + count * stride - block start
;*
;* Regenerate the template in linker.py (_INIT_LOOP) after editing:
;*   cl6x -mv6740 --abi=eabi init_call_loop.asm --output_file=x.obj
;*   and copy the .text section bytes (python -B build/tests/test_init_loop.py
;*   --dump x.obj prints them).
        .nocmp
        .global _zdl_init_loop
        .global _zdl_init_loop_end
        .align 32
_zdl_init_loop:
        ADDKPC.S2   _zdl_init_loop, B4, 0   ; B4 = block start
        ADDK.S2     0x1111, B4              ; FIRST: B4 = first handler
        STW.D2T2    B4, *+B15[3]            ; frame word: next handler
        NOP         5
        .align 32
_zdl_init_loop_top:
        LDW.D2T1    *+B15[2], A4            ; ctx (handler argument)
        NOP         4
        LDW.D1T1    *+A4[31], A1            ; A1 = ctx[31]
        LDW.D2T2    *+B15[3], B4            ; B4 = this handler
        NOP         4
 [!A1]  B.S2        _zdl_init_loop_done     ; firmware table not ready: skip the rest
        NOP         5
        B.S2        B4                      ; call the handler (A4 = ctx)
        ADDKPC.S2   _zdl_init_loop_ret, B3, 0
        NOP         4
        .align 32
_zdl_init_loop_ret:
        LDW.D2T2    *+B15[3], B4
        NOP         4
        ADDK.S2     0x2222, B4              ; STRIDE: next handler
        STW.D2T2    B4, *+B15[3]
        ADDKPC.S2   _zdl_init_loop, B5, 0   ; block start
        ADDK.S2     0x3333, B5              ; END: past the last handler
        CMPLT.L2    B4, B5, B0
 [B0]   B.S2        _zdl_init_loop_top
        NOP         5
_zdl_init_loop_done:
        .align 32
_zdl_init_loop_end:
