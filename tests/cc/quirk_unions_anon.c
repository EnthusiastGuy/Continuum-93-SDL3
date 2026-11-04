struct S { int a; union { short h; char c[2]; }; struct { char x, y; }; };
int main(void) { struct S s; s.h = 0x1234; s.y = 1; return s.c[0] + s.y + sizeof(struct S); }
