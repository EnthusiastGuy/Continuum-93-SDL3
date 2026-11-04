; steps: 200000
; Scroll/roll: each flag combination, negative and oversize amounts, clipped and rejected rectangles.
#ORG 0x080000
    LD KLM, 0xFE05C0
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 0
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, 3
    LD MN, 2
    LD O, 0
    INT 1, A
    LD KLM, 0xFE05F2
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 50
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, -3
    LD MN, -2
    LD O, 0
    INT 1, A
    LD KLM, 0xFE0624
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 100
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, 7
    LD MN, -5
    LD O, 1
    INT 1, A
    LD KLM, 0xFE0656
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 150
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, -7
    LD MN, 5
    LD O, 2
    INT 1, A
    LD KLM, 0xFE0688
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 200
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, -47
    LD MN, 65
    LD O, 3
    INT 1, A
    LD KLM, 0xFE06BA
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 250
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, 100
    LD MN, 0
    LD O, 0
    INT 1, A
    LD KLM, 0xFE06EC
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 300
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, 0
    LD MN, 0
    LD O, 3
    INT 1, A
    LD KLM, 0xFE071E
    LD N, 40
    LD O, 30
    LD A, 1
    LD HIJ, 440
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 350
    LD EF, 0
    LD GH, 40
    LD IJ, 30
    LD KL, 40
    LD MN, 30
    LD O, 3
    INT 1, A
    LD KLM, 0xFFC982
    LD N, 30
    LD O, 30
    LD A, 9
    LD HIJ, 450
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 450
    LD EF, 240
    LD GH, 100
    LD IJ, 100
    LD KL, 5
    LD MN, 5
    LD O, 3
    INT 1, A
    LD KLM, 0xFE6380
    LD N, 20
    LD O, 20
    LD A, 50
    LD HIJ, 460
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 0
    LD EF, 50
    LD GH, 20
    LD IJ, 20
    LD KL, -1
    LD MN, 0
    LD O, 0xFC
    INT 1, A
    LD KLM, 0xFE639E
    LD N, 20
    LD O, 20
    LD A, 50
    LD HIJ, 460
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 30
    LD EF, 50
    LD GH, 0
    LD IJ, 20
    LD KL, 1
    LD MN, 1
    LD O, 3
    INT 1, A
    LD KLM, 0xFE63BC
    LD N, 20
    LD O, 20
    LD A, 50
    LD HIJ, 460
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 480
    LD EF, 50
    LD GH, 20
    LD IJ, 20
    LD KL, 1
    LD MN, 1
    LD O, 3
    INT 1, A
    LD KLM, 0xFE63DA
    LD N, 20
    LD O, 20
    LD A, 50
    LD HIJ, 460
    CALL .Pattern
    LD A, 0x40
    LD B, 0
    LD CD, 90
    LD EF, 270
    LD GH, 20
    LD IJ, 20
    LD KL, 1
    LD MN, 1
    LD O, 3
    INT 1, A
    LD KLM, 0xFC0B80
    LD N, 480
    LD O, 4
    LD A, 0
    LD HIJ, 0
    CALL .Pattern
    LD A, 0x40
    LD B, 1
    LD CD, 0
    LD EF, 0
    LD GH, 480
    LD IJ, 4
    LD KL, -1
    LD MN, 1
    LD O, 1
    INT 1, A
    BREAK
.Pattern
    LD Q, O
.PRow
    LD P, N
.PCol
    LD (KLM), A
    INC A
    INC KLM
    DEC P
    CP P, 0
    JP NE, .PCol
    ADD KLM, HIJ
    DEC Q
    CP Q, 0
    JP NE, .PRow
    RET
