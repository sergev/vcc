//
// genmsp430: the MSP430 code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static void flag(int index)
{
    (void)index; // no backend flags yet
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend msp430 = { NULL, flag, output_ext, msp430_codegen };
    return backend_main(argc, argv, &msp430);
}
