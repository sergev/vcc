//
// AAPCS64 argument classification over TAC types.  One function serves the AArch64
// backend (where a call puts each argument) and the semantic pass (the value of
// __builtin_va_class, which va_arg hands to the runtime), so the two cannot disagree.
// AAPCS (32-bit) has the same homogeneous aggregates, with long double a double.
//
#include "tac.h"

// Size of `t` under AAPCS64 (LP64, binary128 long double); under AAPCS only an FP
// type, an array of one, and a structure are asked about.
static int size_of(const Tac_Type *t, bool aapcs32)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_VOID:
        return 1;
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
        return 2;
    case TAC_TYPE_INT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_FLOAT:
        return 4;
    case TAC_TYPE_LONG_DOUBLE:
        return aapcs32 ? 8 : 16;
    case TAC_TYPE_ARRAY:
        return t->u.array.size * size_of(t->u.array.elem_type, aapcs32);
    case TAC_TYPE_STRUCTURE:
        return t->u.structure.size;
    default:
        return 8;
    }
}

static bool is_fp(const Tac_Type *t)
{
    return t->kind == TAC_TYPE_FLOAT || t->kind == TAC_TYPE_DOUBLE ||
           t->kind == TAC_TYPE_LONG_DOUBLE;
}

// The FP kind `t` counts as in an aggregate: under AAPCS long double is double.
static Tac_TypeKind fp_kind(const Tac_Type *t, bool aapcs32)
{
    return aapcs32 && t->kind == TAC_TYPE_LONG_DOUBLE ? TAC_TYPE_DOUBLE : t->kind;
}

// Count the FP members of `t`, all of kind `*base` (set by the first one found); false
// when some member is not one.  A union counts as its largest member.
static bool hfa_members(const Tac_Type *t, bool aapcs32, Tac_TypeKind *base, int *count)
{
    switch (t->kind) {
    case TAC_TYPE_ARRAY: {
        int n = 0;
        if (t->u.array.size > 0 && !hfa_members(t->u.array.elem_type, aapcs32, base, &n))
            return false;
        *count += n * t->u.array.size;
        return true;
    }
    case TAC_TYPE_STRUCTURE: {
        int most = 0;
        for (const Tac_Member *m = t->u.structure.members; m; m = m->next) {
            int n = 0;
            if (!hfa_members(m->type, aapcs32, base, &n))
                return false;
            if (t->u.structure.is_union)
                most = n > most ? n : most;
            else
                *count += n;
        }
        *count += most;
        return true;
    }
    default:
        if (!is_fp(t) || (*base != TAC_TYPE_VOID && *base != fp_kind(t, aapcs32)))
            return false;
        *base = fp_kind(t, aapcs32);
        *count += 1;
        return true;
    }
}

static int hfa(const Tac_Type *t, bool aapcs32, int *esize)
{
    if (is_fp(t)) {
        *esize = size_of(t, aapcs32);
        return 1;
    }
    if (t->kind != TAC_TYPE_ARRAY && t->kind != TAC_TYPE_STRUCTURE)
        return 0;
    Tac_TypeKind base = TAC_TYPE_VOID;
    int count         = 0;
    if (!hfa_members(t, aapcs32, &base, &count) || count < 1 || count > 4)
        return 0;
    Tac_Type b = { .kind = base };
    *esize     = size_of(&b, aapcs32);
    // Padding would make it something else.
    return size_of(t, aapcs32) == count * *esize ? count : 0;
}

int tac_aapcs64_hfa(const Tac_Type *t, int *esize)
{
    return hfa(t, false, esize);
}

int tac_aapcs64_class(const Tac_Type *t)
{
    int esize;
    int count = tac_aapcs64_hfa(t, &esize);
    if (count)
        return esize * 8 + count;
    if ((t->kind == TAC_TYPE_ARRAY || t->kind == TAC_TYPE_STRUCTURE) && size_of(t, false) > 16)
        return TAC_AAPCS64_BY_REF;
    return TAC_AAPCS64_GENERAL;
}

int tac_aapcs32_class(const Tac_Type *t)
{
    int esize;
    int count = hfa(t, true, &esize);
    return count ? esize * 8 + count : TAC_AAPCS32_CORE;
}
