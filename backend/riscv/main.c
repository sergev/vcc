//
// genriscv: the RISC-V code generator, on the shared backend driver.
//
#include "codegen.h"
#include "driver.h"

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend riscv = { NULL, NULL, output_ext, riscv_codegen };
    return backend_main(argc, argv, &riscv);
}
