static long fib(long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) { return fib(22) == 17711 ? 0 : 1; }
