//
// Floating point, in hardware: binary64 arithmetic, comparisons and conversions are
// instructions, and a float is a binary64 value that ldsf and stsf convert.
//
#include "internal.h"

void gen_fp_binary(Gen *g, const Tac_Instruction *in)
{
    (void)in;
    fatal_error("mmix: %s: floating point is not implemented yet", gen_name(g));
}

void gen_fp_unary(Gen *g, const Tac_Instruction *in)
{
    (void)in;
    fatal_error("mmix: %s: floating point is not implemented yet", gen_name(g));
}

void gen_fp_test(Gen *g, const Tac_Val *v, int reg)
{
    (void)v, (void)reg;
    fatal_error("mmix: %s: floating point is not implemented yet", gen_name(g));
}

void gen_fp_convert(Gen *g, const Tac_Val *src, const Tac_Val *dst, Tac_InstructionKind kind)
{
    (void)src, (void)dst, (void)kind;
    fatal_error("mmix: %s: floating point is not implemented yet", gen_name(g));
}
