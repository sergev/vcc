// The entry of a book program on macOS (aarch64-darwin-tests): main, then its result
// printed after the program's output, as the bare-metal crt0-status.o does, and that
// result as the exit status.  Linked with -e _vcc_status_main.  With putch, the
// book's output routine, which our bare-metal runtime has and libSystem lacks.
#include <stdio.h>

int main(void);

void putch(int c)
{
    putchar(c);
}

int vcc_status_main(void)
{
    int r = main();
    printf("%d\n", r);
    return r;
}
