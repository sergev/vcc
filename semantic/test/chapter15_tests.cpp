//
// Chapter 15 — Arrays / pointer arithmetic: semantic errors.  Imported from "Writing a
// C Compiler" (tests/chapter_15/invalid_types, invalid_types/extra_credit, plus the
// invalid_parse programs that parse cleanly for us and are caught by the type checker).
// Each program parses but violates an array / pointer-arithmetic typing rule.  Tests
// assert on a substring of the fatal-error text.
//
// Reclassified from invalid_parse (the book calls these parse errors; our grammar
// accepts the declarator and the type checker rejects it): array_of_functions,
// array_of_functions_2, parenthesized_array_of_functions, return_array, and the
// abstract cast-to-array declarators malformed_abstract_array_declarator_2 and
// cast_to_array_type_3 (nested parenthesized array abstract declarator).
//
// Fifteen invalid_types / extra_credit programs are accepted by our front end today
// and carry DISABLED_ markers, each with the gap noted.  They fall in four groups:
//   (A) an array lvalue is not rejected as a non-modifiable lvalue — assign_to_array
//       (1/2/3), compound_assign_to_array(_nested), postfix_incr_array(_nested),
//       prefix_decr_array(_nested);
//   (B) incompatible pointer types are not diagnosed in assignment / comparison /
//       subtraction — assign_incompatible_pointer_types, compare_different_pointer_
//       types, sub_different_pointer_types;
//   (C) a redeclaration with a conflicting type is accepted — conflicting_array_
//       declarations, conflicting_function_declarations;
//   (D) a scalar (null-pointer) initializer for a *static* array is accepted —
//       null_ptr_static_array_initializer (the non-static form is caught); and
//   (E) an array of functions passes type checking and is only caught later, during
//       lowering, by a get_size assert (not a clean typecheck diagnostic) —
//       array_of_functions, array_of_functions_2, parenthesized_array_of_functions.
//
#include "typecheck_fixture.h"

// --- reclassified from invalid_parse ----------------------------------------

// (int[3](*))0 — casting to an array type is not a scalar cast.
TEST_F(PipelineTest, Chapter15_MalformedAbstractArrayDeclarator2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    return (int[3](*))0;
}
)"),
                 "cannot cast '");
}

// (long(([2])[3]))arr — the nested parenthesized array abstract declarator parses
// (it denotes long[2][3]); casting to an array type is not a scalar cast.
TEST_F(PipelineTest, Chapter15_CastToArrayType3_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    long arr[6];
    return ((long(([2])[3]))arr);
}
)"),
                 "cannot cast '");
}

// int foo(void)[3]; — a function cannot return an array.
TEST_F(PipelineTest, Chapter15_ReturnArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void)[3];
)"),
                 "function cannot return array type");
}

// int(foo(void))[3][4]; — a function cannot return an array.
TEST_F(PipelineTest, Chapter15_FunctionReturnsArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int(foo(void))[3][4];
)"),
                 "function cannot return array type");
}

// --- pointer arithmetic -----------------------------------------------------

// x + y — it's illegal to add two pointers.
TEST_F(PipelineTest, Chapter15_AddTwoPointers_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *x = 0;
    int *y = 0;
    return (x + y == 0);
}
)"),
                 "invalid operands to '\\+'");
}

// y - 0.0 — you can't subtract a double from a pointer.
TEST_F(PipelineTest, Chapter15_SubDoubleFromPtr_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *y = 0;
    return (y - 0.0 == 0.0);
}
)"),
                 "invalid operands to '-'");
}

// 0 - x — you can't subtract a pointer from an integer.
TEST_F(PipelineTest, Chapter15_SubPtrFromInt_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *x = 0;
    return 0 - x == 0;
}
)"),
                 "invalid operands to '-'");
}

// --- comparisons ------------------------------------------------------------

// arr == &arr — int * and int (*)[10] are not comparable.
TEST_F(PipelineTest, Chapter15_CompareExplicitAndImplicitAddr_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int arr[10];
    return arr == &arr;
}
)"),
                 "incompatible pointer types");
}

// l <= 100ul — you can't compare a pointer to an integer.
TEST_F(PipelineTest, Chapter15_ComparePointerToInt_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    long *l = 0;
    return l <= 100ul;
}
)"),
                 "invalid operands to '");
}

// x > 0 — 0 is not converted to a null pointer in a relational comparison.
TEST_F(PipelineTest, Chapter15_ComparePointerToZero_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *x = 0;
    return x > 0;
}
)"),
                 "invalid operands to '");
}

// --- subscripting -----------------------------------------------------------

// a[4] where a is int — a subscript needs at least one pointer operand.
TEST_F(PipelineTest, Chapter15_SubscriptNonPtr_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 3;
    return a[4];
}
)"),
                 "invalid operands to '\\[\\]'");
}

// ptr[subscript] where both are pointers — a subscript needs exactly one integer.
TEST_F(PipelineTest, Chapter15_SubscriptBothPointers_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int x = 10;
    int *ptr = &x;
    int *subscript = 0;
    return ptr[subscript];
}
)"),
                 "invalid operands to '\\[\\]'");
}

// arr[2.0] — a subscript index must be an integer.
TEST_F(PipelineTest, Chapter15_DoubleSubscript_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {4, 5, 6};
    return arr[2.0];
}
)"),
                 "invalid operands to '\\[\\]'");
}

// --- casts ------------------------------------------------------------------

// (int[10])arr — a cast to array type is illegal.
TEST_F(PipelineTest, Chapter15_CastToArrayType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int arr[10];
    return (int[10])arr;
}
)"),
                 "cannot cast '");
}

// (int *[10])arr — a cast to array-of-pointers type is illegal.
TEST_F(PipelineTest, Chapter15_CastToArrayType2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    long arr[10];
    return (int *[10])arr;
}
)"),
                 "cannot cast '");
}

// --- argument / function typing ---------------------------------------------

// foo(&arr) — a pointer to an array is not a pointer to a pointer.
TEST_F(PipelineTest, Chapter15_BadArgType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int **x) {
    return x[0][0];
}

int main(void) {
    int arr[1] = {10};
    return foo(&arr);
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// --- initializers -----------------------------------------------------------

// int arr[1] = 4; — you can't initialize an array with a scalar.
TEST_F(PipelineTest, Chapter15_ScalarInitializerForArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int arr[1] = 4;
    return arr[0];
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// int arr[1] = 0; — not even a null pointer constant initializes an array.
TEST_F(PipelineTest, Chapter15_NullPtrArrayInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int arr[1] = 0;
    return arr[0];
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// double arr[3] = 1.0; — you can't initialize a static array with a scalar.
TEST_F(PipelineTest, Chapter15_ScalarInitializerForStaticArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(double arr[3] = 1.0;

int main(void)
{
    return 0;
}
)"),
                 "cannot initialize 'double \\[3\\]' with a scalar value");
}

// int x = {1, 2, 3}; — a scalar can't take a multi-element compound initializer.
TEST_F(PipelineTest, Chapter15_CompoundInitializerForScalar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int x = {1, 2, 3};
    return x;
}
)"),
                 "excess elements in scalar initializer");
}

// static int x = {1, 2, 3}; — same for a static scalar.
TEST_F(PipelineTest, Chapter15_CompoundInitializerForStaticScalar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    static int x = {1, 2, 3};
    return x;
}
)"),
                 "excess elements in scalar initializer");
}

// int arr[3] = {1, 2, 3, 4}; — too many elements.
TEST_F(PipelineTest, Chapter15_CompoundInitializerTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3, 4};
    return arr[2];
}
)"),
                 "excess elements in array initializer");
}

// static int arr[3] = {1, 2, 3, 4}; — too many elements (static).
TEST_F(PipelineTest, Chapter15_CompoundInitializerTooLongStatic_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    static int arr[3] = {1, 2, 3, 4};
    return arr[2];
}
)"),
                 "excess elements in array initializer");
}

// int *arr[3] = {0, 0, 1.0}; — a double can't convert to int *.
TEST_F(PipelineTest, Chapter15_IncompatibleElemTypeCompoundInit_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int *arr[3] = {0, 0, 1.0};
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// int *arr[3] = {0, 0, 1.0}; at file scope — a double can't convert to int *.
TEST_F(PipelineTest, Chapter15_IncompatibleElemTypeStaticCompoundInit_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int *arr[3] = {0, 0, 1.0};

int main(void)
{
    return 0;
}
)"),
                 "cannot initialize 'int \\*' with this initializer");
}

// static int arr[3] = {p, p+1, 0}; — a static initializer must be constant.
TEST_F(PipelineTest, Chapter15_StaticNonConstArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int p) {
    static int arr[3] = { p, p + 1, 0};
    return arr[2];
}

int main(void) {
    return foo(5);
}
)"),
                 "initializer element is not a constant expression");
}

// --- extra_credit: compound assignment / switch -----------------------------

// elem += 1.0; — pointer compound add requires an integer RHS.
TEST_F(PipelineTest, Chapter15_CompoundAddDoubleToPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    int *elem = arr;
    elem += 1.0;
    return 0;
}
)"),
                 "invalid operands to '");
}

// elem0 += elem1; — pointer compound add requires an integer RHS, not a pointer.
TEST_F(PipelineTest, Chapter15_CompoundAddTwoPointers_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    int *elem0 = arr;
    int *elem1 = arr + 1;
    elem0 += elem1;
    return 0;
}
)"),
                 "invalid operands to '");
}

// i -= elem; — a pointer can't be the RHS of a compound subtract from an integer.
TEST_F(PipelineTest, Chapter15_CompoundSubPointerFromInt_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    int *elem = arr + 1;
    int i = 0;
    i -= elem;
    return 0;
}
)"),
                 "invalid operands to '");
}

// switch (arr) — you can't switch on an array.
TEST_F(PipelineTest, Chapter15_SwitchOnArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    switch (arr) {
        default:
            return 0;
    }
    return 1;
}
)"),
                 "'switch' requires an integer value");
}

// --- invalid array element types --------------------------------------------

// int foo[3](int a); — an array of functions is rejected cleanly by the type
// checker (validate_type), not left to trip a get_size assert during lowering.
TEST_F(PipelineTest, Chapter15_ArrayOfFunctions_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo[3](int a);
)"),
                 "array of functions");
}

// int (foo[3])(int a); — same array of functions, parenthesized differently.
TEST_F(PipelineTest, Chapter15_ArrayOfFunctions2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int (foo[3])(int a);
)"),
                 "array of functions");
}

// int(foo[3])(int a); — same array of functions.
TEST_F(PipelineTest, Chapter15_ParenthesizedArrayOfFunctions_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int(foo[3])(int a);
)"),
                 "array of functions");
}

// arr = arr2; — an array is not a modifiable lvalue (gap: array assignment accepted).
TEST_F(PipelineTest, Chapter15_AssignToArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int arr[3] = {1, 2, 3};
    int arr2[3] = {4, 5, 6};
    arr = arr2;
    return arr[0];
}
)"),
                 "is not assignable");
}

// dim2[0] = dim; — a nested array is not a modifiable lvalue (gap).
TEST_F(PipelineTest, Chapter15_AssignToArray2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    int dim2[1][2] = {{1, 2}};
    int dim[2] = {3, 4};
    dim2[0] = dim;
    return dim[0];
}
)"),
                 "is not assignable");
}

// *ptr_to_array = arr; — *(int(*)[3]) has array type, not a modifiable lvalue (gap).
TEST_F(PipelineTest, Chapter15_AssignToArray3_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = { 1, 2, 3};
    int (*ptr_to_array)[3];
    *ptr_to_array = arr;
}
)"),
                 "is not assignable");
}

// int (*arr)[3] = &four_element_array; — int(*)[4] is incompatible with int(*)[3].
// (This codebase reports incompatible-pointer assignment as "Cannot convert type for
// assignment", matching the sibling Chapter15_BadArgType_Neg etc.)
TEST_F(PipelineTest, Chapter15_AssignIncompatiblePointerTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int four_element_array[4] = {1, 2, 3, 4};
    int (*arr)[3] = &four_element_array;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// array_ptr < ptr — comparing two different pointer types is illegal.
TEST_F(PipelineTest, Chapter15_CompareDifferentPointerTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    long x = 10;
    long *ptr = &x + 1;
    long(*array_ptr)[10] = (long (*)[10]) &x;
    return array_ptr < ptr;
}
)"),
                 "incompatible pointer types");
}

// ptr - ptr2 — subtracting pointers to different types is illegal.
TEST_F(PipelineTest, Chapter15_SubDifferentPointerTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    long x[10];
    long *ptr = x;
    unsigned long *ptr2 = (unsigned long *)ptr;
    return ptr - ptr2;
}
)"),
                 "incompatible pointer types");
}

// int arr[6]; ... int arr[5]; — a conflicting redeclaration is accepted today (gap).
TEST_F(PipelineTest, Chapter15_ConflictingArrayDeclarations_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int arr[6];

int main(void) {
    return arr[0];
}

int arr[5];
)"),
                 "conflicting types for 'arr'");
}

// int f(int arr[2][3]); int f(int arr[2][4]); — conflicting adjusted parameter types (gap).
TEST_F(PipelineTest, Chapter15_ConflictingFunctionDeclarations_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int f(int arr[2][3]);

int f(int arr[2][4]);
)"),
                 "conflicting types for 'f'");
}

// static int arr[1] = 0; — a scalar can't initialize a static array (the non-static
// form is caught; the static path accepts it today — gap).
TEST_F(PipelineTest, Chapter15_NullPtrStaticArrayInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    static int arr[1] = 0;
    return arr[0];
}
)"),
                 "initialize");
}

// arr -= 1; — an array is not a modifiable lvalue for compound assignment (gap).
TEST_F(PipelineTest, Chapter15_CompoundAssignToArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    arr -= 1;
    0;
}
)"),
                 "is not assignable");
}

// arr[1] += 1; — a nested array is not a modifiable lvalue for compound assignment (gap).
TEST_F(PipelineTest, Chapter15_CompoundAssignToNestedArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    long arr[2][2] = {{1, 2}, {3, 4}};
    arr[1] += 1;
    return 0;
}
)"),
                 "is not assignable");
}

// arr++; — an array is not a modifiable lvalue for ++ (gap).
TEST_F(PipelineTest, Chapter15_PostfixIncrArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    arr++;
    return 0;
}
)"),
                 "is not assignable");
}

// arr[2]++; — a nested array is not a modifiable lvalue for ++ (gap).
TEST_F(PipelineTest, Chapter15_PostfixIncrNestedArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[2][3] = {{1, 2, 3}, {4, 5, 6}};
    arr[2]++;
    return 0;
}
)"),
                 "is not assignable");
}

// --arr; — an array is not a modifiable lvalue for -- (gap).
TEST_F(PipelineTest, Chapter15_PrefixDecrArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[3] = {1, 2, 3};
    --arr;
    return 0;
}
)"),
                 "is not assignable");
}

// --arr[2]; — a nested array is not a modifiable lvalue for -- (gap).
TEST_F(PipelineTest, Chapter15_PrefixDecrNestedArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int arr[2][3] = {{1, 2, 3}, {4, 5, 6}};
    --arr[2];
    return 0;
}
)"),
                 "is not assignable");
}
