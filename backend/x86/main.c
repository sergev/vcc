//
// genx86: the x86-64 code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { NULL, NULL },
};

static void flag(int index)
{
    if (index == 0)
        x86_regalloc = false;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend x86 = { flags, flag, output_ext, x86_codegen };
    return backend_main(argc, argv, &x86);
}
