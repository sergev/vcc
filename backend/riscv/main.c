//
// genriscv: the RISC-V code generator, on the shared backend driver.
//
#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { NULL, NULL },
};

static void flag(int index)
{
    (void)index;
    riscv_regalloc = false;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend riscv = { flags, flag, output_ext, riscv_codegen };
    return backend_main(argc, argv, &riscv);
}
