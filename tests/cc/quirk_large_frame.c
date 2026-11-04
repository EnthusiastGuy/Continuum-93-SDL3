int main(void) { char big[70000]; int x = 1; big[69999] = 2; return big[69999] + x; }
