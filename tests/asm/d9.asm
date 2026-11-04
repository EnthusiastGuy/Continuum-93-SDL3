; D-9: a line with no opcode-table form is an error and emits nothing (the reference emitted only
; its opcode byte, with no error). conformance --asm counts this file as an expected divergence.
#ORG 0x1000
.start  LD A, 1
        LD (BCD), 0
        //INT 0x01, A
        DR
        LD (.start), 0, 1
        JP .start
