//
// Chapter 12 — Unsigned integers: semantic errors.  Imported from "Writing a C
// Compiler" (tests/chapter_12/invalid_types).  Each program parses cleanly but
// fails type checking; tests assert on a substring of the fatal-error text.
//
// Omitted (target-semantics gap, not a negative for us):
//   invalid_labels/extra_credit/switch_duplicate_cases — relies on x86's 32-bit
//   unsigned truncation to collapse two case labels onto the same value
//   (4294967295u and 1099511627775l both become 2**32-1 once converted to a
//   32-bit unsigned int).  Our unsigned int is a single 41-bit word, so
//   1099511627775 (2**40-1) is not truncated, the two cases stay distinct, and
//   there is no duplicate to diagnose — the program is accepted.  Like ch11's
//   bitshift_duplicate_cases / switch_duplicate_cases, it is a candidate for the
//   run tests (task 11b), not a semantic negative here.
//
#include "typecheck_fixture.h"

// --- invalid_types ----------------------------------------------------------

// A file-scope variable declared as both 'unsigned' and 'int' conflicts.
TEST_F(PipelineTest, Chapter12_ConflictingSignedUnsigned_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(unsigned x;

int x;

int main(void) {
    return 0;
}
)"),
                 "conflicting types for 'x'");
}

// A function redeclared with a different return type (unsigned int vs unsigned
// long) conflicts.
TEST_F(PipelineTest, Chapter12_ConflictingUintUlong_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(unsigned int foo(void);

unsigned long foo(void) {
    return 0;
}

int main(void) {
    return 0;
}
)"),
                 "conflicting types for 'foo'");
}
