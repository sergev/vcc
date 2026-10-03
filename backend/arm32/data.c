//
// Static data: variables, constants and block-scope statics.  A long long or a double
// is two words, the low one first; a long double is a double, from its binary128 bits.
//
#include <string.h>

#include "codegen.h"
#include "float128.h"
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

static void emit_words(FILE *out, uint64_t v)
{
    fprintf(out, "    .word   0x%08x, 0x%08x\n", (unsigned)(uint32_t)v, (unsigned)(v >> 32));
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
        fprintf(out, "    .short  %d\n", it->u.short_val);
        return 2;
    case TAC_STATIC_INIT_U16:
        fprintf(out, "    .short  %u\n", it->u.ushort_val);
        return 2;
    case TAC_STATIC_INIT_I32:
        fprintf(out, "    .word   %d\n", it->u.int_val);
        return 4;
    case TAC_STATIC_INIT_U32:
        fprintf(out, "    .word   %u\n", it->u.uint_val);
        return 4;
    case TAC_STATIC_INIT_I64:
        emit_words(out, (uint64_t)it->u.long_val);
        return 8;
    case TAC_STATIC_INIT_U64:
        emit_words(out, it->u.ulong_val);
        return 8;
    case TAC_STATIC_INIT_FLOAT: {
        float f = (float)it->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        fprintf(out, "    .word   0x%08x\n", bits);
        return 4;
    }
    case TAC_STATIC_INIT_DOUBLE:
    case TAC_STATIC_INIT_LONG_DOUBLE: {
        double d = it->kind == TAC_STATIC_INIT_DOUBLE ? it->u.double_val
                                                      : f128_to_double(it->u.long_double_val);
        uint64_t bits;
        memcpy(&bits, &d, 8);
        emit_words(out, bits);
        return 8;
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
        if (it->u.pointer.byte_offset)
            fprintf(out, "    .word   %s%+d\n", it->u.pointer.name, it->u.pointer.byte_offset);
        else
            fprintf(out, "    .word   %s\n", it->u.pointer.name);
        return 4;
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

void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment)
{
    int size  = a32_size(type);
    int align = a32_align(type) > alignment ? a32_align(type) : alignment;
    int log2  = 0;
    while ((1 << log2) < align)
        log2++;
    bool bss = all_zero(init);
    fprintf(out, "    %s\n", readonly ? ".section .rodata" : bss ? ".bss" : ".data");
    if (global)
        fprintf(out, "    .globl  %s\n", name);
    fprintf(out, "    .p2align %d\n", log2);
    fprintf(out, "    .type   %s, %%object\n", name);
    fprintf(out, "    .size   %s, %d\n", name, size);
    fprintf(out, "%s:\n", name);
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
