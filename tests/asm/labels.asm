; Label rules: '.' absolute, '~' relative, case, validity, placement, comments, #DB labels.
#ORG 0x80000
.a
~b      NOP
.Mixed_Case?1 NOP
.c      ; comment after a label
   .e   LD A, 1
.f	LD B, 2
.5      NOP
.?      NOP
._      NOP
        LD ABC, .a
        LD ABC, .mixed_case?1
        LD ABC, .Mixed_Case?1
        LD ABC, ~b
        LD ABC, .b
        JR ~b
        JR .a
        CALLR ~a
        LD A, .5
        LD ABC, .?
        LD ABC, (._)
        LD ABC, .forward
        LD ABC, (.forward + 2)
        LD ABC, (.forward - A)
        LD (.forward), ABC
        ADD ABC, .c
        SUB ABC, .e
        #DB .a, .c, .e, .f, .5
        #DB .forward
.forward NOP
        LD A, .x.y
        LD A, a.b
.end
