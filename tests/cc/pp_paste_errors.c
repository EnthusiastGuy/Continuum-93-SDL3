#define P(a) a ##
#define Q(a, b) a ## b
int main(void) { int x = P(1); return Q(+, -) Q(x, ); }
