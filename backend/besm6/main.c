//
// genbesm: the BESM-6 code generator, on the shared backend driver.
//
#include "codegen.h"
#include "driver.h"

static const BackendFlag flags[] = {
    { "madlen", "Emit Madlen assembly for Dubna" },
    { "unix", "Emit Unix (b6as) assembly (default)" },
    { "bemsh", "Emit Bemsh autocode for Dubna" },
    {},
};

// Unix (b6as) is the default dialect; Madlen stays reachable via --madlen (the
// libc.bin build and the behavioral run tests request it explicitly).
static Besm_Dialect dialect = BESM_UNIX;

static void set_flag(int index)
{
    static const Besm_Dialect dialects[] = { BESM_MADLEN, BESM_UNIX, BESM_BEMSH };
    dialect = dialects[index];
}

// Default output-file extension for each dialect.
static const char *output_ext(void)
{
    switch (dialect) {
    case BESM_UNIX:
        return ".s";
    case BESM_BEMSH:
        return ".bemsh";
    case BESM_MADLEN:
    default:
        return ".mad";
    }
}

static void codegen(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out)
{
    codegen_program(program, tl, out, dialect);
}

int main(int argc, char *argv[])
{
    static const Backend besm6 = { flags, set_flag, output_ext, codegen };
    return backend_main(argc, argv, &besm6);
}
