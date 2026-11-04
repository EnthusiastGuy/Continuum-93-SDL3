int g; char gc; short gs[3];
int main(void) { int *p = &g; *p += 5; gc -= 1; gs[1] <<= 2; gs[2]++; --gs[0]; int i = 0; i++; ++i; i--; p++; return g + gc + gs[1] + i; }
