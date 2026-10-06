//
// AArch64 IR → GNU assembler syntax: ELF, as clang and binutils accept it, or Mach-O,
// as Apple's clang does.
//
#include <inttypes.h>
#include <string.h>

#include "a64.h"

const char *a64_reg_name(int reg, A64_Width width)
{
    static char names[5][A64_VREG][5];
    static const char prefix[] = { 'w', 'x', 's', 'd', 'q' };
    if (reg < 0 || reg >= A64_VREG)
        return NULL;
    char *name = names[width][reg];
    if (!name[0]) {
        bool gpr = width == A64_W || width == A64_X;
        if (reg == A64_SP)
            strcpy(name, width == A64_W ? "wsp" : "sp");
        else if (reg == A64_ZR)
            strcpy(name, width == A64_W ? "wzr" : "xzr");
        else if (gpr && reg >= A64_X0 && reg < A64_SP)
            snprintf(name, 5, "%c%d", prefix[width], reg - A64_X0);
        else if (!gpr && a64_is_fpreg(reg))
            snprintf(name, 5, "%c%d", prefix[width], reg - A64_V0);
        else
            return NULL; // a register used at a width of the other file
    }
    return name;
}

bool a64_macho = false;

void a64_emit_name(FILE *out, const char *name)
{
    if (a64_macho)
        fputc('_', out);
    fputs(name, out);
}

const char *a64_local_prefix(void)
{
    return a64_macho ? "L" : ".L";
}

// Symbol operand `o`: ELF `:lo12:sym+8`, Mach-O `_sym@PAGEOFF+8`.
static void emit_sym(FILE *out, const A64_Operand *o)
{
    static const char *const elf[]   = { "", "", ":lo12:", ":got:" };
    static const char *const macho[] = { "", "@PAGE", "@PAGEOFF", "@GOTPAGE" };
    if (o->label) {
        fputs(o->sym, out);
    } else {
        if (!a64_macho)
            fputs(elf[o->reloc], out);
        a64_emit_name(out, o->sym);
        if (a64_macho)
            fputs(macho[o->reloc], out);
    }
    if (o->imm)
        fprintf(out, "%+" PRId64, o->imm);
}

static const char *const conds[] = { "eq", "ne", "hs", "lo", "mi", "pl", "vs",
                                     "vc", "hi", "ls", "ge", "lt", "gt", "le" };

static void emit_reg(FILE *out, int reg, A64_Width width)
{
    const char *name = a64_reg_name(reg, width);
    if (name)
        fputs(name, out);
    else if (reg >= A64_VREG)
        fprintf(out, "%%%c%d", "wxsdq"[width], reg - A64_VREG);
    else
        fatal_error("aarch64: register %d at width %d", reg, width);
}

static void emit_operand(FILE *out, const A64_Operand *o)
{
    static const char *const shifts[]  = { "lsl", "lsr", "asr" };
    static const char *const extends[] = { "uxtb", "uxth", "uxtw", "uxtx",
                                           "sxtb", "sxth", "sxtw", "sxtx" };
    switch (o->kind) {
    case A64_OPND_NONE:
        break;
    case A64_OPND_REG:
        emit_reg(out, o->reg, o->width);
        break;
    case A64_OPND_IMM:
        fprintf(out, "#%" PRId64, o->imm);
        break;
    case A64_OPND_SYM:
        emit_sym(out, o);
        break;
    case A64_OPND_MEM:
        fputc('[', out);
        emit_reg(out, o->reg, A64_X);
        if (o->sub == A64_MEM_INDEX) {
            fputs(", ", out);
            emit_reg(out, o->index, o->index_width);
            if (o->index_width == A64_W)
                fprintf(out, ", %s", extends[o->ext]);
            else if (o->imm)
                fputs(", lsl", out);
            if (o->imm)
                fprintf(out, " #%" PRId64, o->imm);
            fputc(']', out);
        } else if (o->sub == A64_MEM_GOT) {
            fputs(a64_macho ? ", " : ", :got_lo12:", out);
            a64_emit_name(out, o->sym);
            fputs(a64_macho ? "@GOTPAGEOFF]" : "]", out);
        } else if (o->sub == A64_MEM_POST)
            fprintf(out, "], #%" PRId64, o->imm);
        else if (o->imm || o->sub == A64_MEM_PRE)
            fprintf(out, ", #%" PRId64 "]%s", o->imm, o->sub == A64_MEM_PRE ? "!" : "");
        else
            fputc(']', out);
        break;
    case A64_OPND_SHIFT:
        emit_reg(out, o->reg, o->width);
        fprintf(out, ", %s #%" PRId64, shifts[o->sub], o->imm);
        break;
    case A64_OPND_EXT:
        emit_reg(out, o->reg, o->width);
        fprintf(out, ", %s", extends[o->sub]);
        if (o->imm)
            fprintf(out, " #%" PRId64, o->imm);
        break;
    case A64_OPND_LSL:
        fprintf(out, "lsl #%" PRId64, o->imm);
        break;
    case A64_OPND_COND:
        fputs(conds[o->sub], out);
        break;
    case A64_OPND_FZERO:
        fputs("#0.0", out);
        break;
    }
}

// An instruction: 4-space indent, mnemonic padded to 8 columns.
void a64_emit_instr(FILE *out, const A64_Instr *in)
{
    char mnem[16];
    int first = 0;
    if (in->op == A64_BCOND) {
        snprintf(mnem, sizeof(mnem), "b.%s", conds[in->opnd[0].sub]);
        first = 1;
    } else {
        snprintf(mnem, sizeof(mnem), "%s", a64_mnemonic[in->op]);
    }
    fprintf(out, "    %s", mnem);
    for (int i = first; i < A64_MAX_OPERANDS && in->opnd[i].kind != A64_OPND_NONE; i++) {
        if (i == first) {
            int pad = 8 - (int)strlen(mnem);
            fprintf(out, "%*s", pad > 1 ? pad : 1, "");
        } else {
            fputs(", ", out);
        }
        emit_operand(out, &in->opnd[i]);
    }
    fputc('\n', out);
}

void a64_emit_func(FILE *out, const A64_Func *fn)
{
    fprintf(out, "    .text\n");
    if (fn->global) {
        fprintf(out, "    %-7s ", ".globl");
        a64_emit_name(out, fn->name);
        fputc('\n', out);
    }
    fprintf(out, "    .p2align 2\n");
    if (!a64_macho)
        fprintf(out, "    %-7s %s, @function\n", ".type", fn->name);
    a64_emit_name(out, fn->name);
    fputs(":\n", out);
    for (const A64_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const A64_Instr *in = b->head; in; in = in->next)
            a64_emit_instr(out, in);
    }
    if (!a64_macho)
        fprintf(out, "    %-7s %s, .-%s\n", ".size", fn->name, fn->name);
}
