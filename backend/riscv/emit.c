//
// RISC-V IR → GNU assembler syntax.
//
#include <inttypes.h>
#include <string.h>

#include "rv.h"

const char *rv_reg_name(int reg)
{
    static const char *const names[64] = {
        "zero", "ra",  "sp",  "gp",  "tp",   "t0",   "t1",  "t2",  "s0",  "s1",  "a0",
        "a1",   "a2",  "a3",  "a4",  "a5",   "a6",   "a7",  "s2",  "s3",  "s4",  "s5",
        "s6",   "s7",  "s8",  "s9",  "s10",  "s11",  "t3",  "t4",  "t5",  "t6",  "ft0",
        "ft1",  "ft2", "ft3", "ft4", "ft5",  "ft6",  "ft7", "fs0", "fs1", "fa0", "fa1",
        "fa2",  "fa3", "fa4", "fa5", "fa6",  "fa7",  "fs2", "fs3", "fs4", "fs5", "fs6",
        "fs7",  "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11",
    };
    return reg >= 0 && reg < RV_VREG ? names[reg] : NULL;
}

static void emit_operand(FILE *out, const Rv_Operand *o)
{
    switch (o->kind) {
    case RV_OPND_NONE:
        break;
    case RV_OPND_REG:
        if (o->reg >= RV_VREG)
            fprintf(out, "v%d", o->reg - RV_VREG);
        else
            fputs(rv_reg_name(o->reg), out);
        break;
    case RV_OPND_IMM:
        fprintf(out, "%" PRId64, o->imm);
        break;
    case RV_OPND_SYM:
        fputs(o->sym, out);
        if (o->imm)
            fprintf(out, "%+" PRId64, o->imm);
        break;
    case RV_OPND_MEM:
        fprintf(out, "%" PRId64 "(%s)", o->imm, rv_reg_name(o->reg));
        break;
    }
}

// An instruction or directive: 4-space indent, mnemonic padded to 8 columns.
static void emit_op(FILE *out, const char *op, const char *args)
{
    if (args && *args)
        fprintf(out, "    %-7s %s\n", op, args);
    else
        fprintf(out, "    %s\n", op);
}

static void emit_instr(FILE *out, const Rv_Instr *in)
{
    fprintf(out, "    %s", rv_mnemonic[in->op]);
    for (int i = 0; i < 3 && in->opnd[i].kind != RV_OPND_NONE; i++) {
        if (i == 0) {
            int pad = 8 - (int)strlen(rv_mnemonic[in->op]);
            fprintf(out, "%*s", pad > 1 ? pad : 1, "");
        } else {
            fputs(", ", out);
        }
        emit_operand(out, &in->opnd[i]);
    }
    fputc('\n', out);
}

void rv_emit_func(FILE *out, const Rv_Func *fn)
{
    emit_op(out, ".text", NULL);
    if (fn->global)
        emit_op(out, ".globl", fn->name);
    emit_op(out, ".p2align", "2");
    fprintf(out, "    %-7s %s, @function\n", ".type", fn->name);
    fprintf(out, "%s:\n", fn->name);
    for (const Rv_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const Rv_Instr *in = b->head; in; in = in->next)
            emit_instr(out, in);
    }
    fprintf(out, "    %-7s %s, .-%s\n", ".size", fn->name, fn->name);
}
