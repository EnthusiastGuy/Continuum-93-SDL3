#define STR(x) #x
#define CAT(a, b) a ## b
#define VA(f, ...) f(__VA_ARGS__)
#define EMPTY()
#define SELF SELF + 1
#define TWICE(x) x x
int CAT(fo, o) = 3;
int add(int a, int b) { return a + b; }
char *s = STR( a  "q\n" 'c'  +  b );
int main(void) { int SELF = 0; EMPTY() return VA(add, foo, TWICE(1) ) + SELF; }
