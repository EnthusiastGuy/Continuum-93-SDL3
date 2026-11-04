struct S { int a; } s;
int main(void) { int *p = 0; return 1 ? p : s; }
