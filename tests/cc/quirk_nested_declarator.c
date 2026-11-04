int (*p)[sizeof "abc"];
int (*f(void))[2];
int main(void) { char *s = "x"; return sizeof *p + s[0]; }
