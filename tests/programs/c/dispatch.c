// steps: 2000000
// Halcyon-style code paths: tables of function pointers in structs, unions, enums, switch, static locals,
// goto, nested structs with arrays, pointer arithmetic over structs, char/short sign handling, compound ops.
enum Kind { K_ADD, K_MUL, K_NEG, K_SHIFT, K_COUNT };

typedef int (*Op)(int, int);
typedef struct { const char *name; Op fn; unsigned char flags; } OpEntry;
typedef union { unsigned int u; unsigned char b[4]; short s[2]; } Bits;
typedef struct { short x, y; } Pt;
typedef struct { Pt corner[2]; char tag; int id; } Box;

static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
static int neg(int a, int b) { (void)b; return -a; }
static int shift(int a, int b) { return a >> b; }

static const OpEntry table[K_COUNT] = {
    { "add", add, 1 }, { "mul", mul, 2 }, { "neg", neg, 4 }, { "shift", shift, 8 }
};

int out[24];
Box boxes[5];

static int counter(void) { static int n = 100; return n++; }

static int classify(int v)
{
    switch (v & 7) {
    case 0: return 10;
    case 1: case 2: return 20;
    case 5: v += 3; /* fall through */
    default: return v;
    }
}

int main(void)
{
    int acc = 7;
    for (int k = 0; k < K_COUNT; k++) {
        acc = table[k].fn(acc, k + 1);
        out[k] = acc;
    }
    Bits b;
    b.u = 0x8001FF7Fu;
    out[4] = b.b[0] + b.b[3];
    out[5] = b.s[0];
    out[6] = b.s[1];
    for (int i = 0; i < 5; i++) {
        Box *p = boxes + i;
        p->corner[0].x = (short)(i * -1000);
        p->corner[1].y = (short)(i * 3000);
        p->tag = (char)(200 + i);
        p->id = counter();
    }
    int s = 0;
    for (Box *p = &boxes[4]; p >= boxes; --p) s += p->corner[0].x + p->corner[1].y + p->tag;
    out[7] = s;
    out[8] = boxes[3].id;
    for (int v = 0; v < 8; v++) out[9 + v] = classify(v);
    int n = 0;
again:
    n += 3;
    if (n < 20) goto again;
    out[17] = n;
    unsigned short us = 65535; us += 2; out[18] = us;
    signed char sc = 127; sc++; out[19] = sc;
    int m = 1000; m *= -3; m /= 7; m %= 100; m <<= 3; m |= 1; m ^= 0x55; m &= 0xFFF; out[20] = m;
    out[21] = (int)sizeof(Box) + (int)sizeof(Bits) * 100;
    Op f = acc > 0 ? add : mul;
    out[22] = f(6, 7) + (*table[1].fn)(3, 4);
    out[23] = table[2].flags | table[3].flags;
    return out[17];
}
