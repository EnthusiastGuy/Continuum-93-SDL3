// steps: 2000000
// Recursion, structs, pointers, function pointers and signed arithmetic.
typedef struct { int x, y; } P;
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int apply(int (*f)(int), int v) { return f(v); }
int sq(int v) { return v * v; }
P pts[16];
int out[8];
int main(void) {
    for (int i = 0; i < 16; i++) { pts[i].x = i * 3 - 20; pts[i].y = -i * i + 7; }
    int sx = 0, sy = 0;
    for (P *p = pts; p < pts + 16; p++) { sx += p->x; sy += p->y / 3; }
    out[0] = fib(18);
    out[1] = sx;
    out[2] = sy;
    out[3] = apply(sq, -123);
    out[4] = -7 / 2;
    out[5] = -7 % 3;
    out[6] = (int)((unsigned)0x80000000 >> 4);
    out[7] = (-100) >> 2;
    return 0;
}
