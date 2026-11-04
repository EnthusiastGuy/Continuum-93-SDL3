// steps: 3000000
// Runtime library: string.h, stdlib.h (malloc/calloc/free, atoi, rand), ctype.h, a linked list on the heap.
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct Node { struct Node *next; char name[12]; int value; } Node;

char buf[64], copy[64];
int out[16];

static Node *push(Node *head, const char *name, int value)
{
    Node *n = malloc(sizeof(Node));
    strncpy(n->name, name, sizeof n->name - 1);
    n->name[sizeof n->name - 1] = 0;
    n->value = value;
    n->next = head;
    return n;
}

int main(void)
{
    strcpy(buf, "Continuum");
    strcat(buf, " 93");
    out[0] = (int)strlen(buf);
    out[1] = strcmp(buf, "Continuum 93");
    out[2] = strncmp(buf, "Contin", 6);
    out[3] = strcmp("abc", "abd") < 0;
    char *sp = strchr(buf, ' ');
    out[4] = sp ? (int)(sp - buf) : -1;
    memset(copy, '*', 10);
    memcpy(copy + 2, buf, 4);
    memmove(copy + 1, copy + 2, 8);
    out[5] = memcmp(copy, "*ContC", 5) == 0;
    out[6] = atoi("  -12345xyz") + atoi("+77");
    for (char *p = buf; *p; p++) *p = (char)toupper(*p);
    out[7] = isalpha(buf[0]) + isdigit(buf[10]) * 2 + isspace(buf[9]) * 4 + islower('q') * 8;
    srand(93);
    int r = 0;
    for (int i = 0; i < 8; i++) r ^= rand();
    out[8] = r;
    Node *head = 0;
    const char *names[] = { "alpha", "beta", "gamma", "a-very-long-name" };
    for (int i = 0; i < 4; i++) head = push(head, names[i], i * 10 + 1);
    int sum = 0, len = 0;
    for (Node *n = head; n; n = n->next) { sum += n->value; len += (int)strlen(n->name); }
    out[9] = sum;
    out[10] = len;
    int *zeros = calloc(8, sizeof(int));
    out[11] = zeros[0] + zeros[7];
    while (head) { Node *next = head->next; free(head); head = next; }
    free(zeros);
    out[12] = abs(-41) + abs(41);
    return out[0];
}
