//
// genaarch64: the AArch64 code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { "no-peephole", "skip the peephole pass" },
    { "frame-pointer", "keep a frame record and x29 in every function" },
    { "linux", "hosted Linux: mark the stack non-executable" },
    { NULL, NULL },
};

static void flag(int index)
{
    if (index == 0)
        aarch64_regalloc = false;
    else if (index == 1)
        aarch64_peephole = false;
    else if (index == 2)
        aarch64_frame_pointer = true;
    else
        aarch64_linux = true;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend aarch64 = { flags, flag, output_ext, aarch64_codegen };
    return backend_main(argc, argv, &aarch64);
}
