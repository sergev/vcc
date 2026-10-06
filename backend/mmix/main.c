//
// genmmix: the MMIX code generator, on the shared backend driver.
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
    (void)index;
    mmix_regalloc = false;
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
