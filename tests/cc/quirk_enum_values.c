enum E { A, B = 2147483647, C, D = -5, E2 } e;
enum { X = 'a' * 2 };
int main(void) { switch (e) { case A: case C: case D: return X; default: return E2; } }
