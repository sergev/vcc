//
// genarm32: the ARM32 code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { "frame-pointer", "address the frame from r11, with a frame record" },
    { "no-peephole", "skip the peephole pass" },
    { NULL, NULL },
};

static void flag(int index)
{
    if (index == 0)
        arm32_regalloc = false;
    else if (index == 1)
        arm32_frame_pointer = true;
    else
        arm32_peephole = false;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend arm32 = { flags, flag, output_ext, arm32_codegen };
    return backend_main(argc, argv, &arm32);
}
