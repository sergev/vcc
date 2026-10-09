//
// AVR IR → GNU avr-as syntax (ELF), as clang emits it and its integrated assembler
// accepts it.
//
#include <inttypes.h>
#include <string.h>

#include "avr_ir.h"

// A symbol; quoted when it has a `$` (a static local's name$N, a coroutine's f$resume),
// which GNU avr-as takes only so.
void avr_put_sym(FILE *out, const char *sym)
{
    if (strchr(sym, '$'))
        fprintf(out, "\"%s\"", sym);
    else
        fputs(sym, out);
}

// sym, sym+off or sym-off.
static void emit_sym_off(FILE *out, const char *sym, int64_t off)
{
    avr_put_sym(out, sym);
    if (off > 0)
        fprintf(out, "+%" PRId64, off);
    else if (off < 0)
        fprintf(out, "-%" PRId64, -off);
}

static void emit_operand(FILE *out, const AVR_Operand *o)
{
    static const char *const ptr_name[] = { [AVR_X] = "X", [AVR_Y] = "Y", [AVR_Z] = "Z" };
    static const char *const modifier[] = {
        [AVR_MOD_NONE] = NULL,          [AVR_MOD_LO8] = "lo8",       [AVR_MOD_HI8] = "hi8",
        [AVR_MOD_HH8] = "hh8",          [AVR_MOD_PM_LO8] = "pm_lo8", [AVR_MOD_PM_HI8] = "pm_hi8",
        [AVR_MOD_PM] = "pm",
    };
    switch (o->kind) {
    case AVR_OPND_NONE:
        break;
    case AVR_OPND_REG:
        if (o->reg >= AVR_VREG)
            fprintf(out, "v%d", o->reg - AVR_VREG); // only in a dump before allocation
        else
            fprintf(out, "r%d", o->reg);
        break;
    case AVR_OPND_IMM:
        fprintf(out, "%" PRId64, o->imm);
        break;
    case AVR_OPND_SYM:
        if (o->mod == AVR_MOD_NONE) {
            emit_sym_off(out, o->sym, o->imm);
        } else {
            fprintf(out, "%s(", modifier[o->mod]);
            emit_sym_off(out, o->sym, o->imm);
            fputc(')', out);
        }
        break;
    case AVR_OPND_PTR:
        fprintf(out, "%s%s%s", o->mode == AVR_PTR_PRE_DEC ? "-" : "", ptr_name[o->reg],
                o->mode == AVR_PTR_POST_INC ? "+" : "");
        break;
    case AVR_OPND_DISP:
        fprintf(out, "%s+%" PRId64, ptr_name[o->reg], o->imm);
        break;
    case AVR_OPND_LABEL:
        avr_put_sym(out, o->sym); // a call's callee may be f$resume
        break;
    }
}

// An instruction: 4-space indent, mnemonic padded to 8 columns.
void avr_emit_instr(FILE *out, const AVR_Instr *in)
{
    const char *text = avr_mnemonic[in->op];
    fprintf(out, "    %s", text);
    for (int i = 0; i < AVR_MAX_OPERANDS && in->opnd[i].kind != AVR_OPND_NONE; i++) {
        if (i == 0) {
            int pad = 8 - (int)strlen(text);
            fprintf(out, "%*s", pad > 1 ? pad : 1, "");
        } else {
            fputs(", ", out);
        }
        emit_operand(out, &in->opnd[i]);
    }
    fputc('\n', out);
}

// The register and I/O names clang's output defines, for hand-read and for code that
// expects them.
void avr_emit_header(FILE *out)
{
    fprintf(out, "__tmp_reg__ = 0\n");
    fprintf(out, "__zero_reg__ = 1\n");
    fprintf(out, "__SREG__ = 63\n");
    fprintf(out, "__SP_H__ = 62\n");
    fprintf(out, "__SP_L__ = 61\n");
}

void avr_emit_func(FILE *out, const AVR_Func *fn)
{
    fprintf(out, "    .text\n");
    if (fn->global) {
        fprintf(out, "    %-7s ", ".globl");
        avr_put_sym(out, fn->name);
        fputc('\n', out);
    }
    fprintf(out, "    .p2align 1\n");
    fprintf(out, "    %-7s ", ".type");
    avr_put_sym(out, fn->name);
    fprintf(out, ", @function\n");
    avr_put_sym(out, fn->name);
    fprintf(out, ":\n");
    for (const AVR_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const AVR_Instr *in = b->head; in; in = in->next)
            avr_emit_instr(out, in);
    }
    fprintf(out, "    %-7s ", ".size");
    avr_put_sym(out, fn->name);
    fputs(", .-", out);
    avr_put_sym(out, fn->name);
    fputc('\n', out);
}
