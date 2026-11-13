;******************************************************************************
;* own_handlers.asm -- the effect's own OnOff and knob edit handlers (no code *
;* from Zoom's effects). Assembled by build/linker.py at link time            *
;* (LinkerConfig.own_handlers), placed as one block in .text.                 *
;*                                                                            *
;* Written from the documented edit-handler ABI (repeat98's                   *
;* docs/EDIT-HANDLER-ABI.md), in the shape of Leemuzhko's zui.asm             *
;* (repeat98 build/runtime/, own_runtime). own_stub is TI's call_stub (TI RTS *
;* lib/src/call_stub.asm), notice below.                                      *
;*                                                                            *
;* Firmware contract (each handler call): A4 = the slot's UI state            *
;* (uint32_t state[53]), B3 = return address. state[0] = first argument of    *
;* the read callback, state[1] = params[] base, state[31] = read knob,        *
;* state[21] = value conversion, state[7] = write parameter (tail call).      *
;*                                                                            *
;* own_knob is the one shared knob body: the linker's 16-byte entry stubs     *
;* (one per knob) set B4 = knob id, B7 = params[] byte offset and branch      *
;* here. own_stub keeps B7 (and A0, A7) across the callbacks.                 *
;*                                                                            *
;* Position independent: every branch target is a local label, no            *
;* relocations. The linker replaces OWN_STEPS (max + 1) and OWN_TOP (the      *
;* float bits of top): state[21](top, 0, steps, 0, v) gives raw = v / 100     *
;* for 1.5f / 151 (knob max <= 150) and 2.0f / 201 (max 200, 210).            *
;******************************************************************************
;* call_stub                                                                  *
;*                                                                            *
;* Copyright (c) 2000 Texas Instruments Incorporated                          *
;* http://www.ti.com/                                                         *
;*                                                                            *
;*  Redistribution and  use in source  and binary forms, with  or without     *
;*  modification,  are permitted provided  that the  following conditions     *
;*  are met:                                                                  *
;*                                                                            *
;*     Redistributions  of source  code must  retain the  above copyright     *
;*     notice, this list of conditions and the following disclaimer.          *
;*                                                                            *
;*     Redistributions in binary form  must reproduce the above copyright     *
;*     notice, this  list of conditions  and the following  disclaimer in     *
;*     the  documentation  and/or   other  materials  provided  with  the     *
;*     distribution.                                                          *
;*                                                                            *
;*     Neither the  name of Texas Instruments Incorporated  nor the names     *
;*     of its  contributors may  be used to  endorse or  promote products     *
;*     derived  from   this  software  without   specific  prior  written     *
;*     permission.                                                            *
;*                                                                            *
;*  THIS SOFTWARE  IS PROVIDED BY THE COPYRIGHT  HOLDERS AND CONTRIBUTORS     *
;*  "AS IS"  AND ANY  EXPRESS OR IMPLIED  WARRANTIES, INCLUDING,  BUT NOT     *
;*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     *
;*  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT     *
;*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     *
;*  SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES  (INCLUDING, BUT  NOT     *
;*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     *
;*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     *
;*  THEORY OF  LIABILITY, WHETHER IN CONTRACT, STRICT  LIABILITY, OR TORT     *
;*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
;*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      *
;*                                                                            *
;******************************************************************************

        .sect   ".text"
        .global own_onf, own_knob, own_stub

; ---------------------------------------------------------------------------
; OnOff: v = state[31](state[0], 0); tail call state[7](params, v ? 1.0f :
; 0.0f, K) (params[0]). K = 0x44306666, as every stock handler passes.
; ---------------------------------------------------------------------------
own_onf:
        STW.D2T2    B3,*B15--[2]            ; push return address
        MV.L1       A4,A7                   ; A7 = state (kept by own_stub)
        LDW.D1T2    *+A7[31],B31            ; read-knob callback
        LDW.D1T1    *+A7[1],A0              ; params[] base (kept by own_stub)
        LDW.D1T1    *+A7[0],A4              ; callback argument 0
        MVK.L2      0,B4                    ; knob id 0 = OnOff
        NOP         4
        CALLP.S2    own_stub,B3             ; A4 = on / off
        MV.L2X      A4,B0
        LDW.D1T1    *+A7[7],A3              ; write-parameter callback
        LDW.D2T2    *++B15[2],B3            ; pop return address
        MVKL.S1     0x44306666,A6
        MVKH.S1     0x44306666,A6
        ZERO.L2     B4                      ; 0.0f
  [B0]  MVKH.S2     0x3F800000,B4           ; 1.0f (low half 0)
        MV.L1       A0,A4                   ; &params[0]
        B.S2X       A3                      ; tail call
        NOP         5

; ---------------------------------------------------------------------------
; Knob body. In: A4 = state, B4 = knob id, B7 = params[] byte offset.
;   v = state[31](state[0], knob id)
;   r = state[21](A4 = top, B4 = 0, A6 = steps, B6 = 0, A8 = v)
;   tail call state[7](params + offset, B4 = r, A6 = K)
; ---------------------------------------------------------------------------
        .align  32
own_knob:
        STW.D2T2    B3,*B15--[2]            ; push return address
        MV.L1       A4,A7                   ; A7 = state (kept by own_stub)
        LDW.D1T2    *+A7[31],B31            ; read-knob callback
        LDW.D1T1    *+A7[1],A0              ; params[] base (kept by own_stub)
        LDW.D1T1    *+A7[0],A4              ; callback argument 0
        NOP         4
        CALLP.S2    own_stub,B3             ; A4 = raw knob value
        MV.L1       A4,A8
        LDW.D1T2    *+A7[21],B31            ; value-conversion callback
        MVK.S1      OWN_STEPS,A6
        MVKL.S1     OWN_TOP,A4
        MVKH.S1     OWN_TOP,A4
        ZERO.L2     B4
        ZERO.L2     B6
        CALLP.S2    own_stub,B3             ; A4 = converted value
        LDW.D1T1    *+A7[7],A3              ; write-parameter callback
        LDW.D2T2    *++B15[2],B3            ; pop return address
        MVKL.S2     0x44306666,B5
        MVKH.S2     0x44306666,B5
        MV.L2X      A4,B4
        ADD.L1X     A0,B7,A4                ; &params[offset]
        MV.L1X      B5,A6
        B.S2X       A3                      ; tail call
        NOP         5

; ---------------------------------------------------------------------------
; TI's call_stub: calls B31, keeps A0-A2, A6, A7, B0-B7 for the caller.
; ---------------------------------------------------------------------------
        .align  32
own_stub:
        STW.D2T1    A2,*B15--[2]
        B.S2        B31
||      STDW.D2T1   A7:A6,*B15--
        STDW.D2T1   A1:A0,*B15--
        STDW.D2T2   B7:B6,*B15--
        STDW.D2T2   B5:B4,*B15--
        STDW.D2T2   B1:B0,*B15--
        STDW.D2T2   B3:B2,*B15--
||      ADDKPC.S2   own_stub_ret,B3,0
own_stub_ret:
        LDDW.D2T2   *++B15,B3:B2
        LDDW.D2T2   *++B15,B1:B0
        LDDW.D2T2   *++B15,B5:B4
        LDDW.D2T2   *++B15,B7:B6
        LDDW.D2T1   *++B15,A1:A0
        B.S2        B3
||      LDDW.D2T1   *++B15,A7:A6
        LDW.D2T1    *++B15[2],A2
        NOP         4
