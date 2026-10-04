//
// Static data: variables, constants and block-scope statics.  Every type has alignment
// 1; .rodata lives in SRAM, copied there from flash by crt0 with .data.
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

static void emit_float(FILE *out, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    fprintf(out, "    .long   0x%08x\n", bits);
}

// Whether `name` is a function of the translation unit `program`.
static bool names_function(const Tac_TopLevel *program, const char *name)
{
    for (const Tac_TopLevel *t = program; t; t = t->next) {
        if (t->kind == TAC_TOPLEVEL_FUNCTION && strcmp(t->u.function.name, name) == 0)
            return true;
        if (t->kind == TAC_TOPLEVEL_EXTERN && strcmp(t->u.extern_.name, name) == 0)
            return t->u.extern_.type && t->u.extern_.type->kind == TAC_TYPE_FUN_TYPE;
    }
    return false;
}

// Emit one initializer item; returns its size in bytes.
static int emit_init(FILE *out, const Tac_TopLevel *program, const Tac_StaticInit *it)
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
    case TAC_STATIC_INIT_FLOAT:
        emit_float(out, (float)it->u.float_val);
        return 4;
    case TAC_STATIC_INIT_DOUBLE:
        emit_float(out, (float)it->u.double_val);
        return 4;
    case TAC_STATIC_INIT_LONG_DOUBLE:
        emit_float(out, f128_to_float(it->u.long_double_val));
        return 4;
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
        if (names_function(program, it->u.pointer.name))
            fprintf(out, "    .short  pm(%s)\n", it->u.pointer.name);
        else if (it->u.pointer.byte_offset)
            fprintf(out, "    .short  %s%+d\n", it->u.pointer.name, it->u.pointer.byte_offset);
        else
            fprintf(out, "    .short  %s\n", it->u.pointer.name);
        return 2;
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

void emit_static_variable(FILE *out, const Tac_TopLevel *program, const char *name, bool global,
                          const Tac_Type *type, const Tac_StaticInit *init, bool readonly,
                          int alignment)
{
    int size = avr_type_size(type);
    int log2 = 0;
    while ((1 << log2) < alignment)
        log2++;
    bool bss = all_zero(init);
    fprintf(out, "    %s\n", readonly ? ".section .rodata" : bss ? ".bss" : ".data");
    if (global)
        fprintf(out, "    .globl  %s\n", name);
    if (log2)
        fprintf(out, "    .p2align %d\n", log2);
    fprintf(out, "    .type   %s, @object\n", name);
    fprintf(out, "    .size   %s, %d\n", name, size);
    fprintf(out, "%s:\n", name);
    int n = 0;
    if (bss) {
        n = size;
        if (size > 0)
            fprintf(out, "    .zero   %d\n", size);
    } else {
        for (const Tac_StaticInit *it = init; it; it = it->next)
            n += emit_init(out, program, it);
    }
    if (n < size)
        fprintf(out, "    .zero   %d\n", size - n);
    else if (n == 0)
        fprintf(out, "    .zero   1\n"); // an empty object still gets an address
}
