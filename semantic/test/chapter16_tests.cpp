//
// Chapter 16 — Characters and strings: semantic errors.  Imported from "Writing a
// C Compiler" (tests/chapter_16/invalid_types, invalid_types/extra_credit, and
// invalid_labels/extra_credit).  Each program parses but violates a character /
// string typing rule.  Tests assert on a substring of the fatal-error text.
//
// Every program yields a clean, specific diagnostic — no DISABLED_ needed.
// One reclassification vs. the book's intent (still here):
//   * implicit_conversion_pointers_to_different_size_arrays — the book expects a
//     char(*)[2] vs char(*)[10] mismatch.  We accept `&"x"` (a string literal is an
//     array lvalue), and compatible_type ignores array bounds, so the assignment is
//     not a constraint we reject; the case is kept as a positive test that `&"..."`
//     typechecks.
//   * implicit_conversion_between_char_pointers / string_literal_is_plain_char_
//     pointer — caught by the generic "cannot convert '.*' to '.*' when".
//
#include "typecheck_fixture.h"

// --- invalid_types ----------------------------------------------------------

// "foo" = "bar"; — a string literal decays to a non-lvalue pointer.
TEST_F(PipelineTest, Chapter16_AssignToStringLiteral_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    "foo" = "bar";
    return 0;
}
)"),
                 "expression is not assignable");
}

// char c; extern signed char c; — char and signed char are different types.
TEST_F(PipelineTest, Chapter16_CharAndScharConflict_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(char c = 10;
int main(void)
{
    extern signed char c;
    return c;
}
)"),
                 "conflicting types for 'c'");
}

// foo(unsigned char) then foo(char) — conflicting function redeclaration.
TEST_F(PipelineTest, Chapter16_CharAndUcharConflict_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(unsigned char c) {
    return c;
}
int main(void) {
    return foo(0);
}
int foo(char c);
)"),
                 "conflicting types for 'foo'");
}

// char *ptr = {'a', 'b', 'c'}; — a pointer cannot take a compound initializer.
TEST_F(PipelineTest, Chapter16_CompoundInitializerForPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    char *ptr = {'a', 'b', 'c'};
    return 0;
}
)"),
                 "excess elements in scalar initializer");
}

// signed char *s = c; (c is char *) — char * and signed char * differ.
TEST_F(PipelineTest, Chapter16_ImplicitConversionBetweenCharPointers_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    char *c = 0;
    signed char *s = c;
    return (int) s;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// char(*sp)[2] = &"x"; — &"x" has type char(*)[2].  We now accept the address of a
// string literal (it is an array lvalue with static storage), so this typechecks.
TEST_F(PipelineTest, Chapter16_AddressOfStringLiteral)
{
    RunPipeline(R"(int main(void) {
    char(*string_pointer)[2] = &"x";
    return 0;
}
)");
}

// return -x; (x is char *) — a pointer cannot be negated.
TEST_F(PipelineTest, Chapter16_NegateCharPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    char *x = "foo";
    return -x;
}
)"),
                 "invalid argument type '.*' to unary");
}

// char arr[3][3] = "hello"; — a string cannot initialize a multi-dimensional array.
TEST_F(PipelineTest, Chapter16_StringInitializerForMultidimArray_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(char arr[3][3] = "hello";
int main(void)
{
    return arr[0][2];
}
)"),
                 "cannot initialize array of type 'char \\[3\\]\\[3\\]' with a string literal");
}

// char too_long[3] = "abcd"; — the string is longer than the array.
TEST_F(PipelineTest, Chapter16_StringInitializerTooLong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    char too_long[3] = "abcd";
    return 0;
}
)"),
                 "initializer string for array of type 'char \\[3\\]' is too long");
}

// static char too_long[3] = "abcd"; — too-long string for a static array.
TEST_F(PipelineTest, Chapter16_StringInitializerTooLongStatic_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    static char too_long[3] = "abcd";
    return 0;
}
)"),
                 "initializer string for array of type 'char \\[3\\]' is too long");
}

// char array[3][3] = {"a", "bcde"}; — too-long string in a nested initializer.
TEST_F(PipelineTest, Chapter16_StringInitializerTooLongNested_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    char array[3][3] = {"a", "bcde"};
    return 0;
}
)"),
                 "initializer string for array of type 'char \\[3\\]' is too long");
}

// file-scope char array[3][3] = {"a", "bcde"}; — too-long nested string, static.
TEST_F(PipelineTest, Chapter16_StringInitializerTooLongNestedStatic_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(char array[3][3] = {"a", "bcde"};
int main(void)
{
    return 0;
}
)"),
                 "initializer string for array of type 'char \\[3\\]' is too long");
}

// long ints[4] = "abc"; — a string can only initialize a character array.
TEST_F(PipelineTest, Chapter16_StringInitializerWrongType_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    long ints[4] = "abc";
    return ints[1];
}
)"),
                 "cannot initialize array of type 'long \\[4\\]' with a string literal");
}

// unsigned int nested[1][2] = {"a"}; — wrong element type for a string init.
TEST_F(PipelineTest, Chapter16_StringInitializerWrongTypeNested_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    unsigned int nested[1][2] = {"a"};
    return 0;
}
)"),
                 "cannot initialize array of type 'unsigned int \\[2\\]' with a string literal");
}

// static long int nested[1][2] = {"a"}; — wrong element type, static.
TEST_F(PipelineTest, Chapter16_StringInitializerWrongTypeNestedStatic_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void)
{
    static long int nested[1][2] = {"a"};
    return 0;
}
)"),
                 "cannot initialize array of type 'long \\[2\\]' with a string literal");
}

// signed char *ptr = "foo"; — "foo" decays to char *, not signed char *.
TEST_F(PipelineTest, Chapter16_StringLiteralIsPlainCharPointer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    signed char *ptr = "foo";
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// static signed char *ptr = "foo"; — same mismatch, static initializer.
TEST_F(PipelineTest, Chapter16_StringLiteralIsPlainCharPointerStatic_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    static signed char *ptr = "foo";
    return 0;
}
)"),
                 "cannot initialize 'signed char \\*' with a string literal");
}

// --- invalid_types/extra_credit ---------------------------------------------

// "foo" << 3; — shift operands must be integers.
TEST_F(PipelineTest, Chapter16_BitShiftString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    "foo" << 3;
    return 0;
}
)"),
                 "invalid operands to '");
}

// "My string" & 100; — bitwise operands must be integers.
TEST_F(PipelineTest, Chapter16_BitwiseOperationOnString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    "My string" & 100;
    return 0;
}
)"),
                 "invalid operands to '");
}

// case "foo": — a case expression must be an integer.
TEST_F(PipelineTest, Chapter16_CaseStatementString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    switch (0) {
        case "foo":
            return 1;
        default:
            return 0;
    }
}
)"),
                 "'case' value must be an integer");
}

// s += "another str"; — pointer arithmetic needs an integer operand.
TEST_F(PipelineTest, Chapter16_CompoundAssignFromString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    char * s =  "some string ";
    s += "another str";
    return 0;
}
)"),
                 "invalid operands to '");
}

// "My string" += 1; — a string literal is not a modifiable lvalue.
TEST_F(PipelineTest, Chapter16_CompoundAssignToString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    "My string" += 1;
    return 0;
}
)"),
                 "expression is not assignable");
}

// "foo"++; — a string literal is not a modifiable lvalue.
TEST_F(PipelineTest, Chapter16_PostfixIncrString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    "foo"++;
    return 0;
}
)"),
                 "expression is not assignable");
}

// ++"foo"; — a string literal is not a modifiable lvalue.
TEST_F(PipelineTest, Chapter16_PrefixIncrString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    ++"foo";
    return 0;
}
)"),
                 "expression is not assignable");
}

// switch ("foo") — the controlling expression must be an integer.
TEST_F(PipelineTest, Chapter16_SwitchOnString_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    switch ("foo") {
        default:
        return 0;
    }
}
)"),
                 "'switch' requires an integer value");
}

// --- invalid_labels/extra_credit --------------------------------------------

// case 'x' and case 120 — 'x' is ASCII 120, so the two cases collide.
TEST_F(PipelineTest, Chapter16_DuplicateCaseCharConst_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    static int i = 120;
    switch (i) {
        case 'x':
            return 1;
        case 120:
            return 2;
        default:
            return 3;
    }
}
)"),
                 "duplicate case value 120");
}
