#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <continuum.h>
#include <stdint.h>
typedef struct { char a; int b; } S;
int main(void) { bool t = true; uint8_t u = UINT8_MAX; return (int)offsetof(S, b) + t + u + INT_MIN + (NULL == 0); }
