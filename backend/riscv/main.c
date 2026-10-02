//
// genriscv: the RISC-V code generator, on the shared backend driver.
//
#include <string.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "no-regalloc", "keep every variable in memory" },
    { "no-peephole", "skip the peephole pass" },
    { "frame-pointer", "keep the frame pointer s0" },
    { "rv32", "RV32IMFD/ILP32D (TAC lowered with -t riscv32)" },
    { NULL, NULL },
};

static void flag(int index)
{
    if (index == 0)
        riscv_regalloc = false;
    else if (index == 1)
        riscv_peephole = false;
    else if (index == 2)
        riscv_frame_pointer = true;
    else
        riscv_xlen = 4;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend riscv = { flags, flag, output_ext, riscv_codegen };
    // Installed as vgenriscv64 and vgenriscv32: the name says the width.
    size_t len = strlen(argv[0]);
    if (len >= 2 && strcmp(argv[0] + len - 2, "32") == 0)
        riscv_xlen = 4;
    return backend_main(argc, argv, &riscv);
}
