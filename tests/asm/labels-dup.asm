; A duplicate label stops label collection: every later label is undeclared (encodes 0).
#ORG 0x1000
.one    NOP
.two    NOP
.one    NOP
.three  NOP
        LD ABC, .one
        LD ABC, .two
        LD ABC, .three
        JR .three
