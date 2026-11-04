int a[4]; char *s = "hey"; int *p = &a[2]; int (*fp)(void); int main(void);
int (*mp)(void) = main; char *q = s + 1 - 1; struct { int *x; char c; } st = { a + 1, 'z' };
int main(void) { static int k = 5; static char buf[3]; return *p + k + buf[0]; }
