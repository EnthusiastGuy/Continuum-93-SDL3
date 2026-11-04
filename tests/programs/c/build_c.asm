; steps: 40
; INT 0 0xC2 (Build C) end to end: the C compiler, the assembler and the loader together. Builds the game,
; Halcyon (from ../os, outside the file system root, as the boot selector does) and a missing file.
#ORG 0x900000
    LD A, 0xC2
    LD BCD, .Game
    INT 0, A
    LD (.R1), BCD
    LD (.E1), E
    LD A, 0xC2
    LD BCD, .Os
    INT 0, A
    LD (.R2), BCD
    LD (.E2), E
    LD A, 0xC2
    LD BCD, .Missing
    INT 0, A
    LD (.R3), BCD
    LD (.E3), E
    BREAK
.Game
    #DB "programs/neon-ricochet/neon-ricochet.c", 0
.Os
    #DB "../os/halcyon/halcyon.c", 0
.Missing
    #DB "nope/missing.c", 0
.R1
    #DB 0, 0, 0
.E1
    #DB 0
.R2
    #DB 0, 0, 0
.E2
    #DB 0
.R3
    #DB 0, 0, 0
.E3
    #DB 0
