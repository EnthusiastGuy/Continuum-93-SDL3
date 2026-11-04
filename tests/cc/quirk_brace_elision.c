struct P { int x, y; };
struct P a[2] = { 1, 2, 3, 4 };
int m[2][3] = { 1, 2, 3, { 4 }, 5 };
char names[][4] = { "ab", "cde" };
int main(void) { struct P s = a[1]; struct P t = s; return t.y + m[1][1] + names[1][2]; }
