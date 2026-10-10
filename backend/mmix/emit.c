//
// MMIX IR → GNU mmix-knuth-mmixware-as syntax (ELF), for `as -x -no-predefined-syms`:
// lowercase mnemonics, `$n` registers, decimal immediates, `#hex` wydes, `name:` and
// `L:n` labels.
//
#include <inttypes.h>
#include <stdbool.h>

#include "mmix_ir.h"

// sym, sym+off or sym-off.
static void emit_sym_off(FILE *out, const char *sym, int64_t off)
{
    fputs(sym, out);
    if (off > 0)
        fprintf(out, "+%" PRId64, off);
    else if (off < 0)
        fprintf(out, "-%" PRId64, -off);
}

static void emit_operand(FILE *out, const Mmix_Operand *o)
{
    switch (o->kind) {
    case MMIX_OPND_NONE:
        break;
    case MMIX_OPND_REG:
        if (o->reg >= MMIX_VREG)
            fprintf(out, "v%d", o->reg - MMIX_VREG); // only in a dump before allocation
        else
            fprintf(out, "$%d", o->reg);
        break;
    case MMIX_OPND_IMM:
        fprintf(out, "%" PRId64, o->imm);
        break;
    case MMIX_OPND_WYDE:
        fprintf(out, "#%" PRIx64, o->imm);
        break;
    case MMIX_OPND_SYM:
        emit_sym_off(out, o->sym, o->imm);
        break;
    case MMIX_OPND_LABEL:
        fputs(o->sym, out);
        break;
    case MMIX_OPND_SPECIAL:
        fputs(mmix_special_name[o->reg], out);
        break;
    }
}

static int count_operands(const Mmix_Instr *in)
{
    int n = 0;
    while (n < MMIX_MAX_OPERANDS && in->opnd[n].kind != MMIX_OPND_NONE)
        n++;
    return n;
}

static bool is_reg(const Mmix_Operand *o)
{
    return o->kind == MMIX_OPND_REG && o->reg >= 0;
}

// An immediate in [0, max].
static bool is_imm(const Mmix_Operand *o, int64_t max)
{
    return o->kind == MMIX_OPND_IMM && o->imm >= 0 && o->imm <= max;
}

static bool is_reg_or_byte(const Mmix_Operand *o)
{
    return is_reg(o) || is_imm(o, 255);
}

static bool is_target(const Mmix_Operand *o)
{
    return o->kind == MMIX_OPND_SYM || o->kind == MMIX_OPND_LABEL;
}

// Whether the operands fit the opcode's form.  A wrong immediate would assemble to
// another instruction or not at all, so the emitter checks every one.
static bool operands_ok(const Mmix_Instr *in)
{
    const Mmix_Operand *o = in->opnd;
    int n                 = count_operands(in);
    switch (mmix_form[in->op]) {
    case MMIX_FORM_XYZ:
        return n == 3 && is_reg(&o[0]) && is_reg(&o[1]) && is_reg_or_byte(&o[2]);
    case MMIX_FORM_FP:
        return n == 3 && is_reg(&o[0]) && is_reg(&o[1]) && is_reg(&o[2]);
    case MMIX_FORM_NEG:
        return n == 3 && is_reg(&o[0]) && is_imm(&o[1], 255) && is_reg_or_byte(&o[2]);
    case MMIX_FORM_ROUND:
        return n == 3 && is_reg(&o[0]) && is_imm(&o[1], 4) && is_reg(&o[2]);
    case MMIX_FORM_MEM:
        return (n == 3 && is_reg(&o[0]) && is_reg(&o[1]) && is_reg_or_byte(&o[2])) ||
               (n == 2 && is_reg(&o[0]) && o[1].kind == MMIX_OPND_SYM);
    case MMIX_FORM_XZ:
        return n == 2 && is_reg(&o[0]) && is_reg_or_byte(&o[1]);
    case MMIX_FORM_XY:
        return n == 2 && is_reg(&o[0]) && is_reg(&o[1]);
    case MMIX_FORM_WYDE:
        return n == 2 && is_reg(&o[0]) && o[1].kind == MMIX_OPND_WYDE && o[1].imm >= 0 &&
               o[1].imm <= 0xffff;
    case MMIX_FORM_BRANCH:
        return n == 2 && is_reg(&o[0]) && o[1].kind == MMIX_OPND_LABEL;
    case MMIX_FORM_ADDR:
    case MMIX_FORM_PUSHJ:
        return n == 2 && is_reg(&o[0]) && is_target(&o[1]);
    case MMIX_FORM_JUMP:
        return n == 1 && is_target(&o[0]);
    case MMIX_FORM_POP:
        return n == 2 && is_imm(&o[0], 255) && is_imm(&o[1], 0xffff);
    case MMIX_FORM_GET:
        return n == 2 && is_reg(&o[0]) && o[1].kind == MMIX_OPND_SPECIAL;
    case MMIX_FORM_PUT:
        return n == 2 && o[0].kind == MMIX_OPND_SPECIAL && is_reg_or_byte(&o[1]);
    case MMIX_FORM_TRAP:
        return n == 3 && is_imm(&o[0], 255) && is_imm(&o[1], 255) && is_imm(&o[2], 255);
    case MMIX_FORM_NONE:
        return n == 0;
    }
    return false;
}

// An instruction: four spaces, the mnemonic padded to eight columns, the operands
// separated by ", ".
void mmix_emit_instr(FILE *out, const Mmix_Instr *in)
{
    if (!operands_ok(in))
        internal_error("mmix: wrong operands for %s", mmix_mnemonic[in->op]);
    if (in->opnd[0].kind == MMIX_OPND_NONE) {
        fprintf(out, "    %s\n", mmix_mnemonic[in->op]);
        return;
    }
    fprintf(out, "    %-7s", mmix_mnemonic[in->op]);
    for (int i = 0; i < MMIX_MAX_OPERANDS && in->opnd[i].kind != MMIX_OPND_NONE; i++) {
        fputs(i == 0 ? " " : ", ", out);
        emit_operand(out, &in->opnd[i]);
    }
    fputc('\n', out);
}

void mmix_emit_func(FILE *out, const Mmix_Func *fn)
{
    fprintf(out, "    .text\n");
    if (fn->global)
        fprintf(out, "    .global %s\n", fn->name);
    fprintf(out, "    .p2align 2\n");
    fprintf(out, "%s:\n", fn->name);
    for (const Mmix_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const Mmix_Instr *in = b->head; in; in = in->next)
            mmix_emit_instr(out, in);
    }
}
