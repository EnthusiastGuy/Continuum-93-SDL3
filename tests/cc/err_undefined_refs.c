int g(void); int h(void); extern int v;
int f(void) { return h() + v; }
int main(void) { return g() + f(); }
