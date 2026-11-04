struct S { char b[3]; };
int main(void) { struct S arr[4]; struct S *p = arr + 3; void *v = p; v = v + 1; long n = p - arr; return (int)n + (char *)v - (char *)arr; }
