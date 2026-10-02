#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "semantic.h"
#include "symtab.h"
#include "target.h"

//
// Convert literal to int64
//
int64_t literal_to_int64(const Literal *lit)
{
    switch (lit->kind) {
    case LITERAL_CHAR:
        return (int64_t)lit->u.char_val;
    case LITERAL_INT:
        return (int64_t)lit->u.int_val;
    case LITERAL_LONG:
        return (int64_t)lit->u.long_val;
    case LITERAL_LONG_LONG:
        return (int64_t)lit->u.long_long_val;
    case LITERAL_UINT:
        return (int64_t)lit->u.uint_val;
    case LITERAL_ULONG:
        return (int64_t)lit->u.ulong_val;
    case LITERAL_ULONG_LONG:
        return (int64_t)lit->u.ulong_long_val;
    case LITERAL_FLOAT:
    case LITERAL_DOUBLE:
        return (int64_t)lit->u.real_val;
    case LITERAL_LONG_DOUBLE:
        return f128_to_i64(lit->u.long_double_val, 64);
    case LITERAL_STRING:
        fatal_error("literal_to_int64: Cannot convert string %s", lit->u.string_val);
    case LITERAL_ENUM:
        fatal_error("literal_to_int64: Cannot convert enum %s", lit->u.enum_const);
    default:
        fatal_error("literal_to_int64: Unknown kind %d", lit->kind);
    }
}

//
// Convert literal to uint64
//
uint64_t literal_to_uint64(const Literal *lit)
{
    switch (lit->kind) {
    case LITERAL_CHAR:
        return (uint64_t)lit->u.char_val;
    case LITERAL_INT:
        return (uint64_t)lit->u.int_val;
    case LITERAL_LONG:
        return (uint64_t)lit->u.long_val;
    case LITERAL_LONG_LONG:
        return (uint64_t)lit->u.long_long_val;
    case LITERAL_UINT:
        return (uint64_t)lit->u.uint_val;
    case LITERAL_ULONG:
        return (uint64_t)lit->u.ulong_val;
    case LITERAL_ULONG_LONG:
        return (uint64_t)lit->u.ulong_long_val;
    case LITERAL_FLOAT:
    case LITERAL_DOUBLE:
        return (uint64_t)lit->u.real_val;
    case LITERAL_LONG_DOUBLE:
        return f128_to_u64(lit->u.long_double_val, 64);
    case LITERAL_STRING:
        fatal_error("literal_to_uint64: Cannot convert string %s", lit->u.string_val);
    case LITERAL_ENUM:
        fatal_error("literal_to_uint64: Cannot convert enum %s", lit->u.enum_const);
    default:
        fatal_error("literal_to_uint64: Unknown kind %d", lit->kind);
    }
}

//
// Convert literal to double
//
double literal_to_double(const Literal *lit)
{
    switch (lit->kind) {
    case LITERAL_CHAR:
        return (double)lit->u.char_val;
    case LITERAL_INT:
        return (double)lit->u.int_val;
    case LITERAL_LONG:
        return (double)lit->u.long_val;
    case LITERAL_LONG_LONG:
        return (double)lit->u.long_long_val;
    case LITERAL_UINT:
        return (double)lit->u.uint_val;
    case LITERAL_ULONG:
        return (double)lit->u.ulong_val;
    case LITERAL_ULONG_LONG:
        return (double)lit->u.ulong_long_val;
    case LITERAL_FLOAT:
    case LITERAL_DOUBLE:
        return (double)lit->u.real_val;
    case LITERAL_LONG_DOUBLE:
        return f128_to_double(lit->u.long_double_val);
    case LITERAL_STRING:
        fatal_error("literal_to_double: Cannot convert string %s", lit->u.string_val);
    case LITERAL_ENUM:
        fatal_error("literal_to_double: Cannot convert enum %s", lit->u.enum_const);
    default:
        fatal_error("literal_to_double: Unknown kind %d", lit->kind);
    }
}

Float128 literal_to_long_double(const Literal *lit)
{
    switch (lit->kind) {
    case LITERAL_CHAR:
        return f128_from_i64(lit->u.char_val);
    case LITERAL_INT:
        return f128_from_i64(lit->u.int_val);
    case LITERAL_LONG:
        return f128_from_i64(lit->u.long_val);
    case LITERAL_LONG_LONG:
        return f128_from_i64(lit->u.long_long_val);
    case LITERAL_UINT:
        return f128_from_u64(lit->u.uint_val);
    case LITERAL_ULONG:
        return f128_from_u64(lit->u.ulong_val);
    case LITERAL_ULONG_LONG:
        return f128_from_u64(lit->u.ulong_long_val);
    case LITERAL_FLOAT:
    case LITERAL_DOUBLE:
        return f128_from_double(lit->u.real_val);
    case LITERAL_LONG_DOUBLE:
        return lit->u.long_double_val;
    case LITERAL_STRING:
        fatal_error("literal_to_long_double: Cannot convert string %s", lit->u.string_val);
    case LITERAL_ENUM:
        fatal_error("literal_to_long_double: Cannot convert enum %s", lit->u.enum_const);
    default:
        fatal_error("literal_to_long_double: Unknown kind %d", lit->kind);
    }
}

void check_int_literal_width(const Literal *lit)
{
    if (lit->kind == LITERAL_INT && (uint64_t)lit->u.int_val > 0xFFFFFFFFu &&
        sign_narrow((uint64_t)lit->u.int_val, target_config->int_bits) != lit->u.int_val)
        fatal_error("character constant too long for type int");
    if (lit->kind == LITERAL_UINT && lit->u.uint_val > 0xFFFFFFFFu &&
        unsigned_narrow(lit->u.uint_val, (int)target_config->int_size * 8) != lit->u.uint_val)
        fatal_error("character constant too long for type unsigned int");
}

Tac_StaticInit *new_static_init_int(size_t size, bool is_signed, uint64_t bits)
{
    Tac_StaticInit *result;
    if (size == 4) {
        result = tac_new_static_init(is_signed ? TAC_STATIC_INIT_I32 : TAC_STATIC_INIT_U32);
        if (is_signed)
            result->u.int_val = (int32_t)bits;
        else
            result->u.uint_val = (uint32_t)bits;
    } else {
        result = tac_new_static_init(is_signed ? TAC_STATIC_INIT_I64 : TAC_STATIC_INIT_U64);
        if (is_signed)
            result->u.long_val = (int64_t)bits;
        else
            result->u.ulong_val = bits;
    }
    return result;
}

//
// Convert literal to given arithmetic type and return as Tac_StaticInit.
//
Tac_StaticInit *new_static_init_from_literal(const Type *target_type, const Literal *lit)
{
    if (!is_arithmetic(target_type)) {
        fatal_error("Invalid static initializer for type %d", target_type->kind);
    }

    Tac_StaticInit *result = NULL;
    switch (target_type->kind) {
    case TYPE_BOOL:
        // C11 §6.3.1.2: a scalar converted to _Bool is 0 or 1.  The init slot follows the
        // type's storage, which ast_type_to_tac_type carries in the same widths: a
        // word-sized _Bool uses int's slot, a byte-sized one unsigned char's (only the
        // I8/U8 kinds are packed sub-word by the BESM-6 static emitter).
        if (get_size(target_type) == 1) {
            result              = tac_new_static_init(TAC_STATIC_INIT_U8);
            result->u.uchar_val = (literal_to_int64(lit) != 0);
        } else {
            result = new_static_init_int(get_size(target_type), true, literal_to_int64(lit) != 0);
        }
        break;

    case TYPE_CHAR:
    case TYPE_SCHAR:
        result             = tac_new_static_init(TAC_STATIC_INIT_I8);
        result->u.char_val = (int8_t)literal_to_int64(lit);
        break;

    case TYPE_UCHAR:
        result              = tac_new_static_init(TAC_STATIC_INIT_U8);
        result->u.uchar_val = (uint8_t)literal_to_int64(lit);
        break;

    case TYPE_SHORT:
        result              = tac_new_static_init(TAC_STATIC_INIT_I16);
        result->u.short_val = (int16_t)literal_to_int64(lit);
        break;

    case TYPE_INT:
    case TYPE_ENUM:
        // An enumerated type is int-sized, int-aligned and signed everywhere else
        // (get_size/get_alignment/is_signed, and ast_type_to_tac_type maps it to
        // TAC_TYPE_INT), so it shares int's representation here too.
        result = new_static_init_int(get_size(target_type), true, literal_to_int64(lit));
        break;

    case TYPE_USHORT:
        result               = tac_new_static_init(TAC_STATIC_INIT_U16);
        result->u.ushort_val = (uint16_t)literal_to_int64(lit);
        break;

    case TYPE_LONG:
    case TYPE_LONG_LONG:
        result = new_static_init_int(get_size(target_type), true, literal_to_int64(lit));
        break;

    case TYPE_UINT:
    case TYPE_ULONG:
    case TYPE_ULONG_LONG:
        result = new_static_init_int(get_size(target_type), false, literal_to_uint64(lit));
        break;

    case TYPE_FLOAT:
        result              = tac_new_static_init(TAC_STATIC_INIT_FLOAT);
        result->u.float_val = literal_to_double(lit);
        break;

    case TYPE_DOUBLE:
        result               = tac_new_static_init(TAC_STATIC_INIT_DOUBLE);
        result->u.double_val = literal_to_double(lit);
        break;

    case TYPE_LONG_DOUBLE:
        result                    = tac_new_static_init(TAC_STATIC_INIT_LONG_DOUBLE);
        result->u.long_double_val = literal_to_long_double(lit);
        break;
    default:
        fatal_error("Unsupported constant type for initializer");
    }
    return result;
}
