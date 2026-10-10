//
// Chapter 11 — Long integers: semantic errors.  Imported from "Writing a C
// Compiler" (tests/chapter_11/invalid_types + invalid_labels/extra_credit).
// Each program parses cleanly but fails type checking; tests assert on a
// substring of the fatal-error text.
//
// Reclassification vs. the book:
//   * call_long_as_function — the book declares a file-scope function
//     "long x(void)" and then a local "long x = 0" that shadows it, expecting a
//     "called object is not a function" error.  Our permanent no-shadowing rule
//     rejects the redeclaration first, so we report "Duplicate variable
//     declaration" before ever reaching the call.
//
// Omitted (41-bit int, not negatives for us): bitshift_duplicate_cases and
// switch_duplicate_cases rely on x86's 32-bit truncation to collapse a long
// case value onto an int case (2**35+400 -> 400, 2**34 -> 0).  Our int is
// 41-bit, so those values do not truncate and there is no duplicate; both
// programs are accepted and are candidates for run tests under task 10b.
//
#include "typecheck_fixture.h"

// --- invalid_types ----------------------------------------------------------

// A redeclaration of a name as both a function and a variable is rejected (the
// book intends "x isn't a function", but we reject the duplicate first).
TEST_F(PipelineTest, Chapter11_CallLongAsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(long x(void);

int main(void) {
    long x = 0;
    return x();
}
)"),
                 "Duplicate variable declaration");
}

// The result of a cast expression is not an lvalue.
TEST_F(PipelineTest, Chapter11_CastLvalue_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int i = 0;
    i = (long) i = 10;
    return 0;
}
)"),
                 "expression is not assignable");
}

// A function may not be declared with conflicting parameter types.
TEST_F(PipelineTest, Chapter11_ConflictingFunctionTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a);

int main(void) {
    return 0;
}

int foo(long a);
)"),
                 "Conflicting declarations for function foo");
}

// A global variable may not be redeclared with a different type.
TEST_F(PipelineTest, Chapter11_ConflictingGlobalTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo = 3;

long foo;

int main(void) {
    return foo;
}
)"),
                 "redeclared with different type");
}

// A block-scope "extern" must match the type of the file-scope variable.
TEST_F(PipelineTest, Chapter11_ConflictingVariableTypes_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(long a;

int main(void) {
    extern int a;
    return 0;
}
)"),
                 "redeclared with different type");
}

// --- invalid_labels / extra_credit ------------------------------------------

// Two case labels that differ in spelling but share a value once converted to
// the switch type (long) conflict.
TEST_F(PipelineTest, Chapter11_SwitchDuplicateCases2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int switch_statement(int i) {
    switch((long) i) {
        case 100l: return 0;
        case 100: return 0;
        default: return 1;
    }
}

int main(void) {
    return switch_statement(100);
}
)"),
                 "duplicate case value");
}
