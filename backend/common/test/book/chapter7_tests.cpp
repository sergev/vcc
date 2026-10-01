//
// Chapter 7 — Compound statements: valid programs compiled and run on BESM-6.
// Imported from "Writing a C Compiler" (tests/chapter_7/valid + extra_credit).
// Each program defines int main(void); b6sim --status prints its return value, and we
// compare program output against the value computed by host cc.
//
// Chapter 7 is about variable shadowing in nested blocks, which this compiler
// rejects by design (no identifier shadowing).  Only the programs that do NOT
// shadow an enclosing name run here; the shadowing ones the book lists as valid
// are semantic-negative tests in semantic/chapter7_tests.cpp instead.
//
#include "book_test.h"

// --- valid ------------------------------------------------------------------

// int a; { int b = a = 1; } return a; — 'b' is new, no shadow.
TEST_F(BookTest, Chapter7_DeclarationOnly)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a;
    {
        int b = a = 1;
    }
    return a;
})"));
}

// Empty and nested-empty blocks have no effect on the result.
TEST_F(BookTest, Chapter7_EmptyBlocks)
{
    EXPECT_EQ("30\n", CompileAndRunBook(R"(int main(void) {
    int ten = 10;
    {}
    int twenty = 10 * 2;
    {{}}
    return ten + twenty;
})"));
}

// Two 'b' in sibling (not nested) blocks — allowed, no shadow.
TEST_F(BookTest, Chapter7_MultipleVarsSameName)
{
    EXPECT_EQ("2\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    {
        int b = 4;
        a = b;
    }
    {
        int b = 2;
        a = a - b;
    }
    return a;
})"));
}

// 'b' and 'c' live in disjoint if/else branches — no shadow.
TEST_F(BookTest, Chapter7_NestedIf)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int main(void) {
    int a = 0;
    if (a) {
        int b = 2;
        return b;
    } else {
        int c = 3;
        if (a < c) {
            return !a;
        } else {
            return 5;
        }
    }
    return a;
})"));
}

// 'x' assigned in one inner block, read in a sibling block — no shadow.
TEST_F(BookTest, Chapter7_UseInInnerScope)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int main(void)
{
    int x;
    {
        x = 3;
    }
    {
        return x;
    }
})"));
}

// --- extra_credit -----------------------------------------------------------

// goto jumps between sibling if-blocks; the two 'a' are in sibling scopes.
TEST_F(BookTest, Chapter7_GotoSiblingScope)
{
    EXPECT_EQ("11\n", CompileAndRunBook(R"(int main(void) {
    int sum = 0;
    if (1) {
        int a = 5;
        goto other_if;
        sum = 0;
    first_if:
        a = 5;
        sum = sum + a;
    }
    if (0) {
    other_if:;
        int a = 6;
        sum = sum + a;
        goto first_if;
        sum = 0;
    }
    return sum;
})"));
}
