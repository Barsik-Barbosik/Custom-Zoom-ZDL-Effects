;* init_param_setup.asm -- setup block of the materializing _init.
;*
;* Inserted by build/linker.py (LinkerConfig.init_param_setup) between the
;* prologue and the knob call blocks of themanro's init_materialize.asm:
;*
;*     ctx[34](ctx[1], zeros, 4 * (5 + knobs))
;*
;* Every stock _init makes this call before its handler calls (LineSel:
;* ctx[34](ctx[1], _Fx_FLT_LineSel_Coe, 28) with Coe = 28 zero bytes; Vintage
;* CE, Booster likewise, plus ctx[35] clears). The MS-70CDR tolerates its
;* absence; the G1Xon does not: without it a handler call from _init left the
;* page-1 knobs dead, with it all 9 calls work and saved values load.
;*
;* The zero table is a 64-byte stack frame (56 zero bytes at B15+8, enough
;* for 9 knobs), so there is no relocation. Skipped if ctx[34] is null. On
;* entry (after the prologue) ctx is at *+B15[2]. The block is copied as a
;* unit to a 32-byte aligned address, so the ADDKPC return stays valid.
;* The linker patches the byte count (MVK.S1 at +0x54).
;*
;* Regenerate the template in linker.py (_INIT_SETUP) after editing:
;*   cl6x -mv6740 --abi=eabi init_param_setup.asm --output_file=x.obj
;*   and copy the 160-byte .text section bytes.
        .nocmp
        .global _zdl_init_setup
        .global _zdl_init_setup_end
        .align 32
_zdl_init_setup:
        LDW.D2T1    *+B15[2], A4        ; ctx
        NOP         4
        MV.L1       A4, A5
        ADDK.S1     136, A5             ; A5 = &ctx[34] (offset too big for *+A4[34])
        LDW.D1T1    *A5, A1             ; A1 = ctx[34]
        LDW.D1T1    *+A4[1], A4         ; A4 = ctx[1] (param block)
        NOP         4
 [!A1]  B.S2        _zdl_init_setup_skip   ; no ctx[34]: do nothing
        NOP         5
        ADDK.S2     -64, B15            ; 64 B frame, keeps 8-byte alignment
        ZERO.L1     A6
        ZERO.L1     A7
        STDW.D2T1   A7:A6, *+B15[1]     ; zeros at B15+8 .. B15+63
        STDW.D2T1   A7:A6, *+B15[2]
        STDW.D2T1   A7:A6, *+B15[3]
        STDW.D2T1   A7:A6, *+B15[4]
        STDW.D2T1   A7:A6, *+B15[5]
        STDW.D2T1   A7:A6, *+B15[6]
        STDW.D2T1   A7:A6, *+B15[7]
        MV.L2       B15, B4
        ADDK.S2     8, B4               ; B4 = source (zeros)
        MVK.S1      56, A6              ; A6 = byte count
        B.S2X       A1                  ; ctx[34](A4, B4, A6)
        ADDKPC.S2   _zdl_init_setup_ret, B3, 0
        NOP         4
        .align 32
_zdl_init_setup_ret:
        ADDK.S2     64, B15             ; drop the frame
        NOP
_zdl_init_setup_skip:
        NOP
        .align 32
_zdl_init_setup_end:
