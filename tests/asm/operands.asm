; Operand syntax: number spellings, signs, floats, offsets, registers, special registers, flags.
#ORG 0x80000
.d      LD A, 0x10
        LD A, 0X1f
        LD A, -1
        LD A, +5
        LD A, - 5
        LD AB, -0x10
        LD ABC, 0b101
        LD ABC, 0B101
        LD ABCD, 0o17
        LD A, 0O7
        LD A, -0b1
        LD A, -0o7
        LD ABCD, 99999999999
        LD ABCD, 4294967295
        LD ABCD, -2147483648
        ADD A, 0x000000001
        ADD ABCD, 0x00000000001
        LD A, 0x1FFFFFFFF
        LD A, 1_000
        LD F0, 1.5
        LD F1, -1.5
        LD F2, 5
        LD F3, 1E5
        LD F4, 1E-5
        LD F5, INFINITY
        LD F6, -infinity
        LD F7, nan
        LD F8, .5
        LD F9, 1e39
        LD F015, 2.5
        LD F10, F11
        LD A, F15
        LD A, F16
        LD A, F
        LD F16, 1.0
        ADD F0, 2.5
        ADD F0, -2
        LD A, (ABC + 5)
        LD A, (ABC - 5)
        LD A, (ABC+0x10)
        LD A, (ABC + B)
        LD A, (ABC - B)
        LD A, (ABC + BC)
        LD A, (ABC + BCD)
        LD A, (.d + B)
        LD A, (ABC + .d)
        LD A, (0x1000 + 0x10)
        LD A, (0x1000 - B)
        LD (ABC + 2), A
        LD (ABC - 2), AB
        LD (ABC + D), ABCD
        LD (.d), A
        LD (0x1234), A
        LD A, (0x1234)
        LD (ABC), 0x12, 4
        LD (ABC), 0x12, 4, 2
        LD (ABC), (DEF), 5, GHI
        LD (ABC + 1), 0x12, 4
        LD (ABC + 1), (DEF + 2), 5, GHI
        LD (ABC + 1), (DEF + 2), 5, 6, GHI
        LD (ABC), (DEF + 1), 3, GHI
        LD A,B
        ld   a ,  b
        LD	A, B
        LD	A,B
        LD A, B, C
        LD A, SPR
        LD SPR, ABC
        LD ABC, SPC
        LD ABC, IPO
        LD ABC, (SPR)
        LD A, ZZ
        LD A, ABCDE
        LD A, -B
        LD ABC, +DEF
        LD A, "x"
        LD A, 'x'
        CP A, 1
        CP ABC, .d
        LD A, 1.5.6
        LD A, 3.0
        INC A
        INC ABCD
        DEC (ABC)
        PUSH A
        POP ABC
        PUSH A, Z
        EX A, B
        SL A, .d
        SL A, 3
        INT 0x01, A
        WAIT 10
        WAIT .d
        SETVAR 0xFFFF, ABCD
        MIN A, 5
        MEMF ABC, DEF, G
        REGS 3
        FREGS 1
        VDL 0b00000111
        PLAY .d
        RAND A
        RAND A, 10
