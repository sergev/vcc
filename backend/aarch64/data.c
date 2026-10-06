//
// Static data: variables, constants and block-scope statics.
//
#include <string.h>

#include "codegen.h"
#include "internal.h"

static void emit_ascii(FILE *out, const char *s, size_t len)
{
    fputs("    .ascii  \"", out);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\')
            fprintf(out, "\\%c", c);
        else if (c < ' ' || c > '~')
            fprintf(out, "\\%03o", c);
        else
            fputc(c, out);
    }
    fputs("\"\n", out);
}

// Emit one initializer item; returns its size in bytes.
static int emit_init(FILE *out, const Tac_StaticInit *it)
{
    switch (it->kind) {
    case TAC_STATIC_INIT_I8:
        fprintf(out, "    .byte   %d\n", it->u.char_val);
        return 1;
    case TAC_STATIC_INIT_U8:
        fprintf(out, "    .byte   %u\n", it->u.uchar_val);
        return 1;
    case TAC_STATIC_INIT_I16:
        fprintf(out, "    .hword  %d\n", it->u.short_val);
        return 2;
    case TAC_STATIC_INIT_U16:
        fprintf(out, "    .hword  %u\n", it->u.ushort_val);
        return 2;
    case TAC_STATIC_INIT_I32:
        fprintf(out, "    .word   %d\n", it->u.int_val);
        return 4;
    case TAC_STATIC_INIT_U32:
        fprintf(out, "    .word   %u\n", it->u.uint_val);
        return 4;
    case TAC_STATIC_INIT_I64:
        fprintf(out, "    .xword  %lld\n", (long long)it->u.long_val);
        return 8;
    case TAC_STATIC_INIT_U64:
        fprintf(out, "    .xword  %llu\n", (unsigned long long)it->u.ulong_val);
        return 8;
    case TAC_STATIC_INIT_FLOAT: {
        float f = (float)it->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        fprintf(out, "    .word   0x%08x\n", bits);
        return 4;
    }
    case TAC_STATIC_INIT_DOUBLE: {
        uint64_t bits;
        memcpy(&bits, &it->u.double_val, 8);
        fprintf(out, "    .xword  0x%016llx\n", (unsigned long long)bits);
        return 8;
    }
    case TAC_STATIC_INIT_LONG_DOUBLE: {
        Float128 q = it->u.long_double_val;
        if (aarch64_darwin) { // long double is double
            double d = f128_to_double(q);
            uint64_t bits;
            memcpy(&bits, &d, 8);
            fprintf(out, "    .xword  0x%016llx\n", (unsigned long long)bits);
            return 8;
        }
        fprintf(out, "    .xword  0x%016llx\n    .xword  0x%016llx\n", (unsigned long long)q.lo,
                (unsigned long long)q.hi);
        return 16;
    }
    case TAC_STATIC_INIT_ZERO:
        if (it->u.zero_bytes > 0)
            fprintf(out, "    .zero   %d\n", it->u.zero_bytes);
        return it->u.zero_bytes;
    case TAC_STATIC_INIT_STRING: {
        size_t len = it->u.string.val ? it->u.string.len : 0;
        if (len)
            emit_ascii(out, it->u.string.val, len);
        if (it->u.string.null_terminated) {
            fputs("    .byte   0\n", out);
            len++;
        }
        return (int)len;
    }
    case TAC_STATIC_INIT_POINTER:
    case TAC_STATIC_INIT_FAT_POINTER:
        fputs("    .xword  ", out);
        a64_emit_name(out, it->u.pointer.name);
        if (it->u.pointer.byte_offset)
            fprintf(out, "%+d", it->u.pointer.byte_offset);
        fputc('\n', out);
        return 8;
    }
    return 0;
}

static bool all_zero(const Tac_StaticInit *init)
{
    for (const Tac_StaticInit *it = init; it; it = it->next)
        if (it->kind != TAC_STATIC_INIT_ZERO)
            return false;
    return true;
}

// Whether the initializer holds an address, which the dynamic linker may rebase.
static bool has_address(const Tac_StaticInit *init)
{
    for (const Tac_StaticInit *it = init; it; it = it->next)
        if (it->kind == TAC_STATIC_INIT_POINTER || it->kind == TAC_STATIC_INIT_FAT_POINTER)
            return true;
    return false;
}

// The section of a variable: ELF .rodata, .bss or .data; Mach-O __TEXT,__const, but
// __DATA,__const for one holding an address (ld64 allows no relocation in __TEXT).
static const char *section(bool readonly, bool bss, const Tac_StaticInit *init)
{
    if (!a64_macho)
        return readonly ? ".section .rodata" : bss ? ".bss" : ".data";
    if (readonly)
        return has_address(init) ? ".section __DATA,__const" : ".const";
    return bss ? ".section __DATA,__bss" : ".data";
}

void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment)
{
    int size  = a64_size(type);
    int align = a64_align(type) > alignment ? a64_align(type) : alignment;
    int log2  = 0;
    while ((1 << log2) < align)
        log2++;
    bool bss = all_zero(init);
    fprintf(out, "    %s\n", section(readonly, bss, init));
    if (global) {
        fputs("    .globl  ", out);
        a64_emit_name(out, name);
        fputc('\n', out);
    }
    fprintf(out, "    .p2align %d\n", log2);
    if (!a64_macho) {
        fprintf(out, "    .type   %s, @object\n", name);
        fprintf(out, "    .size   %s, %d\n", name, size);
    }
    a64_emit_name(out, name);
    fputs(":\n", out);
    int n = 0;
    if (bss) {
        n = size;
        if (size > 0)
            fprintf(out, "    .zero   %d\n", size);
    } else {
        for (const Tac_StaticInit *it = init; it; it = it->next)
            n += emit_init(out, it);
    }
    if (n < size)
        fprintf(out, "    .zero   %d\n", size - n);
    else if (n == 0)
        fprintf(out, "    .zero   1\n"); // an empty object still gets an address
}
