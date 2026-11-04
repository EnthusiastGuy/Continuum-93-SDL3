; ".d;comment" is one invalid label token: collection stops.
#ORG 0x1000
.ok     NOP
.d;comment
.after  NOP
        LD ABC, .ok
        LD ABC, .after
