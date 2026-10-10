//
// genmmix: the MMIX code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { "no-peephole", "skip the peephole optimizations" },
    { "frame-pointer", "address the frame from $253" },
    { NULL, NULL },
};

static void flag(int index)
{
    if (index == 0)
        mmix_regalloc = false;
    else if (index == 1)
        mmix_peephole_on = false;
    else
        mmix_frame_pointer = true;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend mmix = { flags, flag, output_ext, mmix_codegen };
    return backend_main(argc, argv, &mmix);
}
