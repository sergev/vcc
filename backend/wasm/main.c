//
// genwasm: the WebAssembly code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { NULL, NULL },
};

static void flag(int index)
{
    (void)index;
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend wasm = { flags, flag, output_ext, wasm_codegen };
    return backend_main(argc, argv, &wasm);
}
