static int helper(void) { return 7; }
int main(void) { asm("CALL .c_helper\n  ; comment\r\n\n"); asm volatile ("NOP" "\nNOP"); return 0; }
