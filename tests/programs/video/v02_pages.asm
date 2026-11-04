; steps: 200000
; Set every page count 0..8, reading count and palette/page addresses after each, then back to 2.
; The oracle never calls Palettes.Initialize(), so the reference loads all-zero palettes; the
; program zeroes every palette area it caused (counts other than 2) so only the reads are compared.
#ORG 0x080000
    LD Q, 0
    LD KLM, 0x100000
.Loop
    LD A, 0x02
    LD B, Q
    INT 1, A
    LD A, 0x01
    INT 1, A
    LD (KLM), A
    INC KLM
    LD A, 0x04
    LD B, 0
    INT 1, A
    LD (KLM), BCD
    ADD KLM, 3
    LD A, 0x04
    LD B, Q
    INT 1, A
    LD (KLM), BCD
    ADD KLM, 3
    LD A, 0x03
    LD B, Q
    INT 1, A
    LD (KLM), BCD
    ADD KLM, 3
    INC Q
    CP Q, 9
    JP NE, .Loop
    LD A, 0x02
    LD B, 2
    INT 1, A
    ; zero the palette blocks of 1 and 3..8 pages: [0x1000000 - 130368 n, +768 n)
    LD A, 0
    LD KLM, 0xFE02C0
.Z1
    LD (KLM), A
    INC KLM
    CP KLM, 0xFE05C0
    JP NE, .Z1
    LD KLM, 0xFA0840
.Z3
    LD (KLM), A
    INC KLM
    CP KLM, 0xFA1140
    JP NE, .Z3
    LD KLM, 0xF80B00
.Z4
    LD (KLM), A
    INC KLM
    CP KLM, 0xF81700
    JP NE, .Z4
    LD KLM, 0xF60DC0
.Z5
    LD (KLM), A
    INC KLM
    CP KLM, 0xF61CC0
    JP NE, .Z5
    LD KLM, 0xF41080
.Z6
    LD (KLM), A
    INC KLM
    CP KLM, 0xF42280
    JP NE, .Z6
    LD KLM, 0xF21340
.Z7
    LD (KLM), A
    INC KLM
    CP KLM, 0xF22840
    JP NE, .Z7
    LD KLM, 0xF01600
.Z8
    LD (KLM), A
    INC KLM
    CP KLM, 0xF02E00
    JP NE, .Z8
    BREAK
