typedef int (*F)(int);
int twice(int x) { return 2 * x; }
F pick(int i) { return i ? twice : 0; }
int main(void) { F f = pick(1); int (*g)() = twice; return f(3) + (*f)(4) + g(5) + pick(1)(6); }
