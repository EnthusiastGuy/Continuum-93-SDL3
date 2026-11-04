char *a = "one" "two" "";
int main(void) { char *b = "x" "y"; char c[] = "q"; return a[3] + b[1] + c[1] + sizeof("abc" "de"); }
