//
// Chapter 14 — Pointers: semantic errors.  Imported from "Writing a C Compiler"
// (tests/chapter_14/invalid_types, invalid_types/extra_credit, and the two
// invalid_declarations/extra_credit label programs).  Each program parses
// cleanly but violates a pointer typing rule — taking the address of a
// non-lvalue, mixing incompatible pointer types, applying an arithmetic/bitwise
// operator to a pointer, dereferencing a non-pointer, etc.  Tests assert on a
// substring of the fatal-error text.
//
// Reclassified vs. the book: two invalid_parse programs parse cleanly for us and
// are caught by the type checker (abstract_function_declarator,
// malformed_function_declarator); the two invalid_declarations label programs use
// a label name as a value, which (labels and identifiers being separate
// namespaces) reports "Symbol not found", like chapter 6's goto_variable.
//
#include "typecheck_fixture.h"

// --- reclassified from invalid_parse ----------------------------------------

// (int (void)) 0; — casting to a function type: not a scalar cast target.
TEST_F(PipelineTest, Chapter14_AbstractFunctionDeclarator_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    (int (void)) 0;
    return 0;
}
)"),
                 "cannot cast '");
}

// int (foo(void))(void); — a function cannot return a function.
TEST_F(PipelineTest, Chapter14_MalformedFunctionDeclarator_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int (foo(void))(void);

int main(void) {
    return 0;
}
)"),
                 "function cannot return function type");
}

// --- invalid_declarations/extra_credit (label used as a value) --------------

// &lbl — it's illegal to take the address of a label.
TEST_F(PipelineTest, Chapter14_AddrOfLabel_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    lbl:
    x = 1;
    if (&lbl == 0) {
        return 1;
    }
    goto lbl;
    return 0;
}
)"),
                 "use of undeclared identifier");
}

// *lbl — it's illegal to dereference a label.
TEST_F(PipelineTest, Chapter14_DerefLabel_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    lbl:
    *lbl;
    return 0;
}
)"),
                 "use of undeclared identifier");
}

// --- invalid_types: address-of a non-lvalue ---------------------------------

// &(&x) — the result of & is not an lvalue.
TEST_F(PipelineTest, Chapter14_AddressOfAddress_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    int *y = &x;
    int **z = &(&x);
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &(x = y) — the result of an assignment is not an lvalue.
TEST_F(PipelineTest, Chapter14_AddressOfAssignment_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    int y = 0;
    int *ptr = &(x = y);
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &10 — a constant is not an lvalue.
TEST_F(PipelineTest, Chapter14_AddressOfConstant_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *ptr = &10;
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &(x ? y : z) — the result of a ternary is not an lvalue.
TEST_F(PipelineTest, Chapter14_AddressOfTernary_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 1;
    int y = 2;
    int z = 3;
    int *ptr = &(x ? y : z);
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &x = 10; — an address-of expression is not an assignable lvalue.
TEST_F(PipelineTest, Chapter14_AssignToAddress_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int x = 0;
    &x = 10;
}
)"),
                 "expression is not assignable");
}

// --- invalid_types: bad integer/pointer conversions -------------------------

// x = 10; where x is int * — cannot assign an integer to a pointer.
TEST_F(PipelineTest, Chapter14_AssignIntToPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x;
    x = 10;
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// int *ptr = x; — a non-constant int is not a null pointer constant.
TEST_F(PipelineTest, Chapter14_AssignIntVarToPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int x = 0;
    int *ptr = x;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// l = d; where l is long *, d is double * — incompatible pointer types.
TEST_F(PipelineTest, Chapter14_AssignWrongPointerType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    double *d = 0;
    long *l = 0;
    l = d;
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// int *x = 0.0; — a double is not a null pointer constant.
TEST_F(PipelineTest, Chapter14_BadNullPointerConstant_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *x = 0.0;
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// int *ptr = 140732898195768ul; — a non-zero integer is not a null pointer constant.
TEST_F(PipelineTest, Chapter14_InvalidPointerInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *ptr = 140732898195768ul;
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// static int *x = 10; — a non-zero integer is not a valid static pointer initializer.
TEST_F(PipelineTest, Chapter14_InvalidStaticInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(static int *x = 10;

int main(void) {
    return 0;
}
)"),
                 "null pointer constant");
}

// f(&x) where f wants int — cannot implicitly convert a pointer to an integer.
TEST_F(PipelineTest, Chapter14_PassPointerAsInt_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int f(int i) {
    return i;
}

int main(void) {
    int x;
    return f(&x);
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// return &i; from a long *-returning function where i is int — wrong pointer type.
TEST_F(PipelineTest, Chapter14_ReturnWrongPointerType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int i;

long *return_long_pointer(void) {
    return &i;
}

int main(void) {
    long *l = return_long_pointer();
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// --- invalid_types: bad pointer/double casts --------------------------------

// (int *) d where d is double — cannot cast a double to a pointer.
TEST_F(PipelineTest, Chapter14_CastDoubleToPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    double d = 0.0;
    int *x = (int *) d;
    return 0;
}
)"),
                 "cannot cast '");
}

// (double) x where x is int * — cannot cast a pointer to a double.
TEST_F(PipelineTest, Chapter14_CastPointerToDouble_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x;
    double d = (double) x;
    return 0;
}
)"),
                 "cannot cast '");
}

// --- invalid_types: incompatible pointer comparisons / ternary --------------

// x == y where x is int *, y is unsigned * — incompatible pointer types.
TEST_F(PipelineTest, Chapter14_CompareMixedPointerTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0ul;
    unsigned *y = 0ul;
    return x == y;
}
)"),
                 "incompatible pointer types");
}

// ptr == ul comparing a pointer to an unsigned long — incompatible types.
TEST_F(PipelineTest, Chapter14_ComparePointerToUlong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *ptr = 0ul;
    unsigned long ul = 0ul;
    return ptr == ul;
}
)"),
                 "incompatible pointer types");
}

// 1 ? x : y with long * and int * operands — incompatible pointer types.
TEST_F(PipelineTest, Chapter14_TernaryMixedPointerTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    long *x = 0;
    int *y = 0;
    int *result = 1 ? x : y;
    return 0;
}
)"),
                 "incompatible pointer types");
}

// --- invalid_types: arithmetic/unary operators on pointers ------------------

// ~x where x is a pointer — bitwise complement is integer-only.
TEST_F(PipelineTest, Chapter14_ComplementPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    return (int) ~x;
}
)"),
                 "to unary '~'");
}

// *l where l is unsigned long — cannot dereference a non-pointer.
TEST_F(PipelineTest, Chapter14_DereferenceNonPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    unsigned long l = 100ul;
    return *l;
}
)"),
                 "to unary '\\*'");
}

// y / 8 where y is a pointer — cannot divide a pointer.
TEST_F(PipelineTest, Chapter14_DividePointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int x = 10;
    int *y = &x;
    (y / 8);
    return 0;
}
)"),
                 "invalid operands to '[*/%]'");
}

// x * y where both are pointers — cannot multiply pointers.
TEST_F(PipelineTest, Chapter14_MultiplyPointers_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    int *y = x;
    (x * y);
    return 0;
}
)"),
                 "invalid operands to '[*/%]'");
}

// 0 * x where x is a pointer — cannot multiply by a pointer.
TEST_F(PipelineTest, Chapter14_MultiplyPointers2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *x = 0;
    (0 * x);
    return 0;
}
)"),
                 "invalid operands to '[*/%]'");
}

// -x where x is a pointer — cannot negate a pointer.
TEST_F(PipelineTest, Chapter14_NegatePointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    -x;
    return 0;
}
)"),
                 "invalid argument type '.*' to unary");
}

// --- invalid_types/extra_credit: bitwise operators on pointers --------------

// 10 & ptr — bitwise AND rejects a pointer operand.
TEST_F(PipelineTest, Chapter14_BitwiseAndPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    long *ptr = 0;
    10 & ptr;
    return 0;
}
)"),
                 "invalid operands to '");
}

// x | y where both are pointers — bitwise OR rejects pointers.
TEST_F(PipelineTest, Chapter14_BitwiseOrPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    int *y = 0;
    x | y;
    return 0;
}
)"),
                 "invalid operands to '");
}

// ptr ^ l where ptr is a pointer — bitwise XOR rejects a pointer operand.
TEST_F(PipelineTest, Chapter14_BitwiseXorPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    unsigned long *ptr = 0;
    long l = 100;
    ptr ^ l;
    return 0;
}
)"),
                 "invalid operands to '");
}

// i >> ptr — a pointer cannot be a shift operand.
TEST_F(PipelineTest, Chapter14_BitwiseLshiftPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *ptr = 0;
    int i = 1000;
    i >> ptr;
    return 0;
}
)"),
                 "invalid operands to '");
}

// x >> 10 where x is a pointer — a pointer cannot be shifted.
TEST_F(PipelineTest, Chapter14_BitwiseRshiftPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    return (int) (x >> 10);
}
)"),
                 "invalid operands to '");
}

// --- invalid_types/extra_credit: compound assignment on pointers ------------

// ptr &= 0 — no bitwise compound assignment on a pointer.
TEST_F(PipelineTest, Chapter14_BitwiseCompoundAssignToPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    int *ptr = &x;
    ptr &= 0;
    return 0;
}
)"),
                 "invalid operands to '");
}

// x |= null where null is a pointer — no bitwise compound assignment with a pointer.
TEST_F(PipelineTest, Chapter14_BitwiseCompoundAssignWithPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *null = 0;
    int x = 100;
    x |= null;
    return 1;
}
)"),
                 "invalid operands to '");
}

// x /= y where both are pointers — no /= on a pointer.
TEST_F(PipelineTest, Chapter14_CompoundDividePointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    int *y = 0;
    x /= y;
    return 0;
}
)"),
                 "invalid operands to '");
}

// i %= ptr where ptr is a pointer — no %= with a pointer.
TEST_F(PipelineTest, Chapter14_CompoundModPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 10;
    int *ptr = &i;
    i %= ptr;
    return 0;
}
)"),
                 "invalid operands to '");
}

// x *= 2 where x is a pointer — no *= on a pointer.
TEST_F(PipelineTest, Chapter14_CompoundMultiplyPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    x *= 2;
    return 0;
}
)"),
                 "invalid operands to '");
}

// --- invalid_types/extra_credit: address-of a non-lvalue --------------------

// &(*ptr -= 10) — the result of a compound assignment is not an lvalue.
TEST_F(PipelineTest, Chapter14_CompoundAssignThruPtrNotLval_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 100;
    int *ptr = &i;
    int *ptr2 = &(*ptr -= 10);
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &(i += 200) — the result of a compound assignment is not an lvalue.
TEST_F(PipelineTest, Chapter14_CompoundAssignmentNotLval_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 100;
    int *ptr = &(i += 200);
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &i-- — the result of a postfix -- is not an lvalue.
TEST_F(PipelineTest, Chapter14_PostfixDecrNotLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 10;
    int *ptr = &i--;
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// &++i — the result of a prefix ++ is not an lvalue.
TEST_F(PipelineTest, Chapter14_PrefixIncrNotLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 10;
    int *ptr = &++i;
    return 0;
}
)"),
                 "cannot take the address of an rvalue");
}

// --- invalid_types/extra_credit: switch on a pointer ------------------------

// switch(x) where x is a pointer — the controlling expression must be an integer.
TEST_F(PipelineTest, Chapter14_SwitchOnPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int *x = 0;
    switch(x) {
        case 0: return 0;
        default: return 1;
    }
}
)"),
                 "'switch' requires an integer value");
}
