//
// ARM32 IR → GNU unified assembler syntax (ELF, as clang and binutils accept it).
//
#include <inttypes.h>
#include <string.h>

#include "a32.h"

const char *a32_reg_name(int reg, A32_Width width)
{
    static char names[3][A32_VREG][12]; // room for any int
    static const char *const special[] = { "sp", "lr", "pc" };
    if (reg < 0 || reg >= A32_VREG)
        return NULL;
    char *name = names[width][reg];
    if (!name[0]) {
        if (width == A32_CORE && reg >= A32_SP && reg <= A32_PC)
            strcpy(name, special[reg - A32_SP]);
        else if (width == A32_CORE && reg < A32_SP)
            snprintf(name, sizeof names[0][0], "r%d", reg);
        else if (width == A32_S && a32_is_vfp(reg))
            snprintf(name, sizeof names[0][0], "s%d", reg - A32_S0);
        else if (width == A32_D && a32_is_vfp(reg) && (reg - A32_S0) % 2 == 0)
            snprintf(name, sizeof names[0][0], "d%d", (reg - A32_S0) / 2);
        else
            return NULL; // the other file, or an odd single at double width
    }
    return name;
}

static void emit_reg(FILE *out, int reg, A32_Width width)
{
    const char *name = a32_reg_name(reg, width);
    if (name)
        fputs(name, out);
    else if (reg >= A32_VREG)
        fprintf(out, "%%%c%d", "rsd"[width], reg - A32_VREG);
    else
        internal_error("arm32: register %d at width %d", reg, width);
}

static const char *const shifts[] = { "lsl", "lsr", "asr", "ror" };

static void emit_reglist(FILE *out, unsigned mask, A32_Width width)
{
    fputc('{', out);
    const char *sep = "";
    for (int r = 0; r <= A32_PC; r++) {
        if (mask & (1u << r)) {
            fprintf(
                out, "%s%s", sep,
                width == A32_D ? a32_reg_name(A32_S0 + 2 * r, A32_D) : a32_reg_name(r, A32_CORE));
            sep = ", ";
        }
    }
    fputc('}', out);
}

static void emit_mem(FILE *out, const A32_Operand *o)
{
    fputc('[', out);
    emit_reg(out, o->reg, A32_CORE);
    if (o->reg2 >= 0) {
        fprintf(out, ", %s", o->negative ? "-" : "");
        emit_reg(out, o->reg2, A32_CORE);
        if (o->imm)
            fprintf(out, ", lsl #%" PRId64, o->imm);
        fputc(']', out);
    } else if (o->sub == A32_MEM_POST) {
        fprintf(out, "], #%" PRId64, o->imm);
    } else if (o->imm || o->sub == A32_MEM_PRE) {
        fprintf(out, ", #%" PRId64 "]%s", o->imm, o->sub == A32_MEM_PRE ? "!" : "");
    } else {
        fputc(']', out);
    }
}

static void emit_operand(FILE *out, const A32_Operand *o)
{
    switch (o->kind) {
    case A32_OPND_NONE:
        break;
    case A32_OPND_REG:
        emit_reg(out, o->reg, o->width);
        break;
    case A32_OPND_IMM:
        fprintf(out, "#%" PRId64, o->imm);
        break;
    case A32_OPND_FPIMM: {
        double d;
        memcpy(&d, &o->imm, sizeof d);
        // The assembler takes an FP immediate only with a point.
        char text[32];
        snprintf(text, sizeof text, "%.9g", d);
        fprintf(out, "#%s%s", text, strpbrk(text, ".e") ? "" : ".0");
        break;
    }
    case A32_OPND_SYM:
        if (o->sub == A32_RELOC_LOWER16)
            fputs("#:lower16:", out);
        else if (o->sub == A32_RELOC_UPPER16)
            fputs("#:upper16:", out);
        fputs(o->sym, out);
        if (o->imm)
            fprintf(out, "%+" PRId64, o->imm);
        break;
    case A32_OPND_MEM:
        emit_mem(out, o);
        break;
    case A32_OPND_SHIFT:
        emit_reg(out, o->reg, A32_CORE);
        fprintf(out, ", %s ", shifts[o->sub]);
        if (o->reg2 >= 0)
            emit_reg(out, o->reg2, A32_CORE);
        else
            fprintf(out, "#%" PRId64, o->imm);
        break;
    case A32_OPND_REGLIST:
        emit_reglist(out, (unsigned)o->imm, o->width);
        break;
    }
}

// An instruction: 4-space indent, mnemonic padded to 8 columns.  The S suffix and the
// condition follow the base mnemonic and precede a `.type` suffix (`vaddeq.f64`).
void a32_emit_instr(FILE *out, const A32_Instr *in)
{
    static const char *const conds[] = { "",   "eq", "ne", "hs", "lo", "mi", "pl", "vs",
                                         "vc", "hi", "ls", "ge", "lt", "gt", "le" };
    const char *mnem                 = a32_mnemonic[in->op];
    const char *dot                  = strchr(mnem, '.');
    int base                         = dot ? (int)(dot - mnem) : (int)strlen(mnem);
    char text[24];
    snprintf(text, sizeof text, "%.*s%s%s%s", base, mnem, in->set_flags ? "s" : "", conds[in->cond],
             mnem + base);
    fprintf(out, "    %s", text);
    for (int i = 0; i < A32_MAX_OPERANDS && in->opnd[i].kind != A32_OPND_NONE; i++) {
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

// What clang writes for armv7a-none-eabihf, and the architecture, so that the file
// assembles alike with or without -mcpu.  A plain .s gets the CPU and FPU attributes
// from the command line but none of the ABI ones.
void a32_emit_header(FILE *out)
{
    fputs(
        "    .syntax unified\n"
        "    .arch   armv7-a\n"
        "    .arch_extension idiv\n"
        "    .fpu    vfpv3-d16\n"
        "    .eabi_attribute Tag_ABI_PCS_R9_use, 0\n"
        "    .eabi_attribute Tag_ABI_PCS_GOT_use, 1\n"
        "    .eabi_attribute Tag_ABI_PCS_wchar_t, 4\n"
        "    .eabi_attribute Tag_ABI_FP_denormal, 1\n"
        "    .eabi_attribute Tag_ABI_FP_exceptions, 0\n"
        "    .eabi_attribute Tag_ABI_FP_number_model, 3\n"
        "    .eabi_attribute Tag_ABI_align_needed, 1\n"
        "    .eabi_attribute Tag_ABI_align_preserved, 1\n"
        "    .eabi_attribute Tag_ABI_enum_size, 2\n"
        "    .eabi_attribute Tag_ABI_VFP_args, 1\n"
        "    .eabi_attribute Tag_ABI_FP_16bit_format, 1\n"
        "    .arm\n",
        out);
}

void a32_emit_func(FILE *out, const A32_Func *fn)
{
    fprintf(out, "    .text\n");
    if (fn->global)
        fprintf(out, "    %-7s %s\n", ".globl", fn->name);
    fprintf(out, "    .p2align 2\n");
    fprintf(out, "    %-7s %s, %%function\n", ".type", fn->name);
    fprintf(out, "%s:\n", fn->name);
    for (const A32_Block *b = fn->blocks; b; b = b->next) {
        if (b->label)
            fprintf(out, "%s:\n", b->label);
        for (const A32_Instr *in = b->head; in; in = in->next)
            a32_emit_instr(out, in);
    }
    fprintf(out, "    %-7s %s, .-%s\n", ".size", fn->name, fn->name);
}
