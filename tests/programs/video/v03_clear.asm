; steps: 200
; Clear pages: current ones, transparent 0, a page beyond the page count.
#ORG 0x080000
    LD A, 0x05
    LD B, 0
    LD C, 0x11
    INT 1, A
    LD A, 0x05
    LD B, 1
    LD C, 0xFF
    INT 1, A
    LD A, 0x05
    LD B, 1
    LD C, 0
    INT 1, A
    LD A, 0x05
    LD B, 4
    LD C, 0x5A
    INT 1, A
    BREAK
