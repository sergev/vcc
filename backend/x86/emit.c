//
// x86-64 IR → GNU/LLVM AT&T assembler syntax (ELF, as clang and binutils accept it).
//
#include <inttypes.h>
#include <string.h>

#include "x86.h"

const char *x86_reg_name(int reg, X86_Width width)
{
    // The legacy eight, by width: rax..rdi, then their low bytes.  With a REX prefix,
    // which the assembler adds for %sil, %dil, %spl and %bpl, the high bytes (%ah..)
    // cannot be named at all, so they never are.
    static const char *const legacy[4][8] = {
        { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil" },
        { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" },
        { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" },
        { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" },
    };
    static char names[4][X86_VREG][8];
    if (reg < 0 || reg >= X86_VREG)
        return NULL;
    if (reg < X86_R8)
        return legacy[width][reg];
    char *name = names[width][reg];
    if (!name[0]) {
        if (reg <= X86_R15)
            snprintf(name, 8, "r%d%s", reg, (const char *const[]){ "b", "w", "d", "" }[width]);
        else if (x86_is_xmm(reg))
            snprintf(name, 8, "xmm%d", reg - X86_XMM0);
        else if (reg == X86_ST0)
            strcpy(name, "st");
        else if (x86_is_st(reg))
            snprintf(name, 8, "st(%d)", reg - X86_ST0);
        else
            return NULL;
    }
    return name;
}

static void emit_reg(FILE *out, int reg, X86_Width width)
{
    const char *name = x86_reg_name(reg, width);
    if (name)
        fprintf(out, "%%%s", name);
    else if (reg >= X86_VREG)
        fprintf(out, "%%v%d%c", reg - X86_VREG, "bwlq"[width]);
    else
        fatal_error("x86: register %d at width %d", reg, width);
}

static void emit_mem(FILE *out, const X86_Operand *o)
{
    if (o->imm || (o->reg < 0 && o->index < 0))
        fprintf(out, "%" PRId64, o->imm);
    if (o->reg < 0 && o->index < 0)
        return; // an absolute address
    fputc('(', out);
    if (o->reg >= 0)
        emit_reg(out, o->reg, X86_Q);
    if (o->index >= 0) {
        fputc(',', out);
        emit_reg(out, o->index, X86_Q);
        fprintf(out, ",%d", o->scale);
    }
    fputc(')', out);
}

static void emit_operand(FILE *out, const X86_Operand *o)
{
    switch (o->kind) {
    case X86_OPND_NONE:
        break;
    case X86_OPND_REG:
        emit_reg(out, o->reg, o->width);
        break;
    case X86_OPND_IMM:
        fprintf(out, "$%" PRId64, o->imm);
        break;
    case X86_OPND_MEM:
        emit_mem(out, o);
        break;
    case X86_OPND_RIP:
        fputs(o->sym, out);
        if (o->imm)
            fprintf(out, "%+" PRId64, o->imm);
        fputs("(%rip)", out);
        break;
    case X86_OPND_LABEL:
        fputs(o->sym, out);
        break;
    }
}

// An instruction: 4-space indent, mnemonic padded to 8 columns.
void x86_emit_instr(FILE *out, const X86_Instr *in)
{
    char text[24];
    snprintf(text, sizeof text, "%s%s", x86_mnemonic[in->op],
             x86_suffixed[in->op] ? (const char *const[]){ "b", "w", "l", "q" }[in->width] : "");
    fprintf(out, "    %s", text);
    for (int i = 0; i < X86_MAX_OPERANDS && in->opnd[i].kind != X86_OPND_NONE; i++) {
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

// Nothing is needed: AT&T syntax and 64-bit code are the defaults for the triple.
void x86_emit_header(FILE *out)
{
    (void)out;
}

void x86_emit_func(FILE *out, const X86_Func *fn)
{
    fprintf(out, "    .text\n");
    if (fn->global)
        fprintf(out, "    %-7s %s\n", ".globl", fn->name);
    fprintf(out, "    .p2align 4\n");
    fprintf(out, "    %-7s %s, @function\n", ".type", fn->name);
    fprintf(out, "%s:\n", fn->name);
    for (const X86_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const X86_Instr *in = b->head; in; in = in->next)
            x86_emit_instr(out, in);
    }
    fprintf(out, "    %-7s %s, .-%s\n", ".size", fn->name, fn->name);
}
