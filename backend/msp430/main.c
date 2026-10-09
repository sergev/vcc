//
// genmsp430: the MSP430 code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { "no-peephole", "skip the peephole pass" },
    { "frame-pointer", "address the frame from r4" },
    { NULL, NULL },
};

static void flag(int index)
{
    if (index == 0)
        msp430_regalloc = false;
    else if (index == 1)
        msp430_peephole = false;
    else
        msp430_frame_pointer = true;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend msp430 = { flags, flag, output_ext, msp430_codegen };
    return backend_main(argc, argv, &msp430);
}
