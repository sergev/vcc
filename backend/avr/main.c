//
// genavr: the AVR code generator, on the shared backend driver.
//
#include <stddef.h>

#include "codegen.h"
#include "driver.h"

static void flag(int index)
{
    (void)index; // no backend flags yet
}

static const char *output_ext(void)
{
    return ".s";
}

int main(int argc, char *argv[])
{
    static const Backend avr = { NULL, flag, output_ext, avr_codegen };
    return backend_main(argc, argv, &avr);
}
