//
// MSP430 IR → GNU msp430-as syntax (ELF), which GNU as and clang's integrated assembler
// both accept, and encode alike.
//
#include <inttypes.h>
#include <string.h>

#include "msp_ir.h"

// sym, sym+off or sym-off; a bare number without a symbol.
static void emit_sym_off(FILE *out, const char *sym, int64_t off)
{
    if (!sym) {
        fprintf(out, "%" PRId64, off);
        return;
    }
    fputs(sym, out);
    if (off > 0)
        fprintf(out, "+%" PRId64, off);
    else if (off < 0)
        fprintf(out, "-%" PRId64, -off);
}

static void emit_reg(FILE *out, int reg)
{
    if (reg >= MSP_VREG)
        fprintf(out, "v%d", reg - MSP_VREG); // only in a dump before allocation
    else
        fprintf(out, "r%d", reg);
}

static void emit_operand(FILE *out, const Msp_Operand *o, bool byte, bool source)
{
    if (source && msp_zero_indexed(o)) {
        fputc('@', out);
        emit_reg(out, o->reg);
        return;
    }
    switch (o->kind) {
    case MSP_OPND_NONE:
        break;
    case MSP_OPND_REG:
        emit_reg(out, o->reg);
        break;
    case MSP_OPND_INDEXED:
        emit_sym_off(out, o->sym, o->imm);
        fputc('(', out);
        emit_reg(out, o->reg);
        fputc(')', out);
        break;
    case MSP_OPND_ABS:
        fputc('&', out);
        emit_sym_off(out, o->sym, o->imm);
        break;
    case MSP_OPND_IND:
    case MSP_OPND_POSTINC:
        fputc('@', out);
        emit_reg(out, o->reg);
        if (o->kind == MSP_OPND_POSTINC)
            fputc('+', out);
        break;
    case MSP_OPND_IMM:
        fputc('#', out);
        emit_sym_off(out, o->sym, o->sym ? o->imm : msp_imm_value(o->imm, byte));
        break;
    case MSP_OPND_LABEL:
        fputs(o->sym, out);
        break;
    }
}

// An instruction: 4-space indent, mnemonic (with .b) padded to 8 columns.  A 0(rN) in a
// source field prints as @rN (msp_zero_indexed); rla/rlc of one is spelt out as the add
// it is, since only its source half can be @rN.
void msp_emit_instr(FILE *out, const Msp_Instr *in)
{
    Msp_Form form    = msp_form[in->op];
    const char *mnem = msp_mnemonic[in->op];
    Msp_Operand opnd[MSP_MAX_OPERANDS] = { in->opnd[0], in->opnd[1] };
    if (form == MSP_FORM_TWICE && msp_zero_indexed(&in->opnd[0])) {
        mnem    = in->op == MSP_RLA ? "add" : "addc";
        form    = MSP_FORM_DOUBLE;
        opnd[1] = in->opnd[0];
    }

    char text[16];
    snprintf(text, sizeof text, "%s%s", mnem, in->byte ? ".b" : "");
    fprintf(out, "    %s", text);
    for (int i = 0; i < MSP_MAX_OPERANDS && opnd[i].kind != MSP_OPND_NONE; i++) {
        if (i == 0) {
            int pad = 8 - (int)strlen(text);
            fprintf(out, "%*s", pad > 1 ? pad : 1, "");
        } else {
            fputs(", ", out);
        }
        bool source = i == 0 && (form == MSP_FORM_DOUBLE || form == MSP_FORM_SINGLE);
        emit_operand(out, &opnd[i], in->byte, source);
    }
    fputc('\n', out);
}

void msp_emit_func(FILE *out, const Msp_Func *fn)
{
    fprintf(out, "    .text\n");
    if (fn->global)
        fprintf(out, "    %-7s %s\n", ".globl", fn->name);
    fprintf(out, "    .p2align 1\n");
    fprintf(out, "    %-7s %s, @function\n", ".type", fn->name);
    fprintf(out, "%s:\n", fn->name);
    for (const Msp_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const Msp_Instr *in = b->head; in; in = in->next)
            msp_emit_instr(out, in);
    }
    fprintf(out, "    %-7s %s, .-%s\n", ".size", fn->name, fn->name);
}
