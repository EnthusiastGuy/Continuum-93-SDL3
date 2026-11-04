#include <string.h>
size_t strlen(const char *s) { return 42; }
int main(void) { return (int)strlen("abc") + abs(-3); }
int abs(int x);
