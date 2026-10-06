//
// Static data: variables, constants and block-scope statics, in GNU as directives.  The
// assembler is big-endian, so .short, .long and .quad carry the byte order.  Read-only
// constants go to .rodata, which the linker puts in the text segment; code reaches them
// with geta, which needs a 4-aligned target.
//
#include <string.h>

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

static void emit_double(FILE *out, double d)
{
    uint64_t bits;
    memcpy(&bits, &d, 8);
    fprintf(out, "    .quad   0x%016llx\n", (unsigned long long)bits);
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
        fprintf(out, "    .long   %d\n", it->u.int_val);
        return 4;
    case TAC_STATIC_INIT_U32:
        fprintf(out, "    .long   %u\n", it->u.uint_val);
        return 4;
    case TAC_STATIC_INIT_I64:
        fprintf(out, "    .quad   %lld\n", (long long)it->u.long_val);
        return 8;
    case TAC_STATIC_INIT_U64:
        fprintf(out, "    .quad   %llu\n", (unsigned long long)it->u.ulong_val);
        return 8;
    case TAC_STATIC_INIT_FLOAT: {
        float f = (float)it->u.float_val;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        fprintf(out, "    .long   0x%08x\n", bits);
        return 4;
    }
    case TAC_STATIC_INIT_DOUBLE:
        emit_double(out, it->u.double_val);
        return 8;
    case TAC_STATIC_INIT_LONG_DOUBLE:
        emit_double(out, f128_to_double(it->u.long_double_val));
        return 8;
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
        // A function's address too: one address space, byte addresses.
        if (it->u.pointer.byte_offset)
            fprintf(out, "    .quad   %s%+d\n", it->u.pointer.name, it->u.pointer.byte_offset);
        else
            fprintf(out, "    .quad   %s\n", it->u.pointer.name);
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

void emit_static_variable(FILE *out, const char *name, bool global, const Tac_Type *type,
                          const Tac_StaticInit *init, bool readonly, int alignment)
{
    int size  = mmix_type_size(type);
    int align = mmix_type_align(type);
    if (alignment > align)
        align = alignment;
    if (readonly && align < 4)
        align = 4; // geta reaches multiples of 4 only
    int log2 = 0;
    while ((1 << log2) < align)
        log2++;
    bool bss = !readonly && all_zero(init);
    fprintf(out, "    %s\n", readonly ? ".section .rodata" : bss ? ".bss" : ".data");
    if (global)
        fprintf(out, "    .global %s\n", name);
    if (log2)
        fprintf(out, "    .p2align %d\n", log2);
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
