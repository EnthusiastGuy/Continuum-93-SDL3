// steps: 3000000
// CRC32 over a generated buffer, a bubble sort and string handling: exercises the C runtime.
#include <string.h>
unsigned int crc32(const unsigned char *p, int n) {
    unsigned int c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}
unsigned char buf[256];
int arr[40];
char text[64];
unsigned int results[4];
int main(void) {
    for (int i = 0; i < 256; i++) buf[i] = (unsigned char)(i * 37 + 11);
    for (int i = 0; i < 40; i++) arr[i] = (i * 7919) % 101 - 50;
    for (int i = 0; i < 40; i++)
        for (int j = 0; j + 1 < 40 - i; j++)
            if (arr[j] > arr[j + 1]) { int t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t; }
    strcpy(text, "Continuum 93 ");
    strcat(text, "SDL3 port");
    results[0] = crc32(buf, 256);
    results[1] = (unsigned int)arr[0] + (unsigned int)arr[39] * 1000u;
    results[2] = (unsigned int)strlen(text);
    results[3] = crc32((const unsigned char *)text, (int)strlen(text));
    return 0;
}
