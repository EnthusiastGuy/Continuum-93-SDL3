; ".g,LD" is one invalid label token: label collection stops there.
#ORG 0x1000
.ok  NOP
.g,LD C, 3
.after NOP
  LD ABC, .ok
  LD ABC, .after
