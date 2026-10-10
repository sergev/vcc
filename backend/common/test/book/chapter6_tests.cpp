//
// Chapter 6 — Conditionals: valid programs compiled and run on BESM-6.
// Imported from "Writing a C Compiler" (tests/chapter_6/valid + extra_credit).
// Each program defines int main(void); b6sim --status prints its return value, and we
// compare program output against the value computed by reasoning / host cc.
//
#include "book_test.h"

// --- valid ------------------------------------------------------------------

TEST_F(BookTest, Chapter6_AssignTernary)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { int a = 0; a = 1 ? 2 : 3; return a; }"));
}

TEST_F(BookTest, Chapter6_BinaryCondition)
{
    EXPECT_EQ("5\n", CompileAndRunBook("int main(void) { if (1 + 2 == 3) return 5; return 0; }"));
}

TEST_F(BookTest, Chapter6_Else)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook("int main(void) { int a = 0; if (a) return 1; else return 2; }"));
}

TEST_F(BookTest, Chapter6_IfNested)
{
    EXPECT_EQ(
        "1\n",
        CompileAndRunBook(
            "int main(void) { int a = 1; int b = 0; if (a) b = 1; else if (b) b = 2; return b; }"));
}

TEST_F(BookTest, Chapter6_IfNested2)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { int a = 0; int b = 1; if (a) b = 1; else "
                                       "if (~b) b = 2; return b; }"));
}

TEST_F(BookTest, Chapter6_IfNested3)
{
    EXPECT_EQ(
        "3\n",
        CompileAndRunBook(
            "int main(void) { int a = 0; if ((a = 1)) if (a == 1) a = 3; else a = 4; return a; }"));
}

TEST_F(BookTest, Chapter6_IfNested4)
{
    EXPECT_EQ(
        "4\n",
        CompileAndRunBook(
            "int main(void) { int a = 0; if (!a) if (3 / 4) a = 3; else a = 8 / 2; return a; }"));
}

TEST_F(BookTest, Chapter6_IfNested5)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 0; if (0) if (0) a = 3; else a = "
                                       "4; else a = 1; return a; }"));
}

TEST_F(BookTest, Chapter6_IfNotTaken)
{
    EXPECT_EQ("0\n", CompileAndRunBook(
                         "int main(void) { int a = 0; int b = 0; if (a) b = 1; return b; }"));
}

TEST_F(BookTest, Chapter6_IfNullBody)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { int x = 0; if (0) ; else x = 1; return x; }"));
}

TEST_F(BookTest, Chapter6_IfTaken)
{
    EXPECT_EQ("1\n", CompileAndRunBook(
                         "int main(void) { int a = 1; int b = 0; if (a) b = 1; return b; }"));
}

TEST_F(BookTest, Chapter6_LhAssignment)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int x = 10; int y = 0; y = (x = 5) ? x : "
                                       "2; return (x == 5 && y == 5); }"));
}

TEST_F(BookTest, Chapter6_MultipleIf)
{
    EXPECT_EQ("8\n",
              CompileAndRunBook("int main(void) { int a = 0; int b = 0; if (a) a = 2; else a = 3; "
                                "if (b) b = 4; else b = 5; return a + b; }"));
}

TEST_F(BookTest, Chapter6_NestedTernary)
{
    EXPECT_EQ("7\n", CompileAndRunBook("int main(void) { int a = 1; int b = 2; int flag = 0; "
                                       "return a > b ? 5 : flag ? 6 : 7; }"));
}

TEST_F(BookTest, Chapter6_NestedTernary2)
{
    EXPECT_EQ("15\n", CompileAndRunBook("int main(void) { int a = 1 ? 2 ? 3 : 4 : 5; int b = 0 ? 2 "
                                        "? 3 : 4 : 5; return a * b; }"));
}

TEST_F(BookTest, Chapter6_RhAssignment)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook(
                  "int main(void) { int flag = 1; int a = 0; flag ? a = 1 : (a = 0); return a; }"));
}

TEST_F(BookTest, Chapter6_Ternary)
{
    EXPECT_EQ("4\n", CompileAndRunBook("int main(void) { int a = 0; return a > -1 ? 4 : 5; }"));
}

TEST_F(BookTest, Chapter6_TernaryMiddleAssignment)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook("int main(void) { int a = 1; a != 2 ? a = 2 : 0; return a; }"));
}

TEST_F(BookTest, Chapter6_TernaryMiddleBinop)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { int a = 1 ? 3 % 2 : 4; return a; }"));
}

TEST_F(BookTest, Chapter6_TernaryPrecedence)
{
    EXPECT_EQ("20\n", CompileAndRunBook("int main(void) { int a = 10; return a || 0 ? 20 : 0; }"));
}

TEST_F(BookTest, Chapter6_TernaryRhBinop)
{
    EXPECT_EQ("1\n", CompileAndRunBook("int main(void) { return 0 ? 1 : 0 || 2; }"));
}

TEST_F(BookTest, Chapter6_TernaryShortCircuit)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook(
                  "int main(void) { int a = 1; int b = 0; a ? (b = 1) : (b = 2); return b; }"));
}

TEST_F(BookTest, Chapter6_TernaryShortCircuit2)
{
    EXPECT_EQ("2\n",
              CompileAndRunBook(
                  "int main(void) { int a = 0; int b = 0; a ? (b = 1) : (b = 2); return b; }"));
}

// binary_false_condition: the if is not taken and main falls off the end with no
// explicit return, so main implicitly returns 0 (C11 §5.1.2.2.3) — matching the host.
TEST_F(BookTest, Chapter6_BinaryFalseCondition)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { if (1 + 2 == 4) return 5; }"));
}

// --- valid/extra_credit -----------------------------------------------------

TEST_F(BookTest, Chapter6_BitwiseTernary)
{
    EXPECT_EQ(
        "5\n",
        CompileAndRunBook(
            "int main(void) { int result; 1 ^ 1 ? result = 4 : (result = 5); return result; }"));
}

TEST_F(BookTest, Chapter6_CompoundAssignTernary)
{
    EXPECT_EQ("8\n", CompileAndRunBook("int main(void) { int a = 4; a *= 1 ? 2 : 3; return a; }"));
}

TEST_F(BookTest, Chapter6_CompoundIfExpression)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { int a = 0; if (a += 1) return a; return 10; }"));
}

TEST_F(BookTest, Chapter6_GotoAfterDeclaration)
{
    EXPECT_EQ("1\n", CompileAndRunBook(
                         "int main(void) { int x = 1; goto post_declaration; int i = (x = 0); "
                         "post_declaration: i = 5; return (x == 1 && i == 5); }"));
}

TEST_F(BookTest, Chapter6_GotoBackwards)
{
    EXPECT_EQ("5\n", CompileAndRunBook(
                         "int main(void) { if (0) label: return 5; goto label; return 0; }"));
}

TEST_F(BookTest, Chapter6_GotoLabel)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { goto label; return 0; label: return 1; }"));
}

TEST_F(BookTest, Chapter6_GotoLabelAndVar)
{
    EXPECT_EQ("5\n",
              CompileAndRunBook(
                  "int main(void) { int ident = 5; goto ident; return 0; ident: return ident; }"));
}

TEST_F(BookTest, Chapter6_GotoLabelMain)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { goto main; return 5; main: return 0; }"));
}

TEST_F(BookTest, Chapter6_GotoLabelMain2)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { goto _main; return 0; _main: return 1; }"));
}

TEST_F(BookTest, Chapter6_GotoNestedLabel)
{
    EXPECT_EQ("5\n", CompileAndRunBook(
                         "int main(void) { goto labelB; labelA: labelB: return 5; return 0; }"));
}

TEST_F(BookTest, Chapter6_LabelAllStatements)
{
    EXPECT_EQ("100\n",
              CompileAndRunBook(
                  "int main(void) { int a = 1; label_if: if (a) goto label_expression; else goto "
                  "label_empty; "
                  "label_goto: goto label_return; if (0) label_expression: a = 0; goto label_if; "
                  "label_return: return a; label_empty:; a = 100; goto label_goto; }"));
}

TEST_F(BookTest, Chapter6_LabelToken)
{
    EXPECT_EQ("1\n",
              CompileAndRunBook("int main(void) { goto _foo_1_; return 0; _foo_1_: return 1; }"));
}

TEST_F(BookTest, Chapter6_LhCompoundAssignment)
{
    EXPECT_EQ("1\n", CompileAndRunBook(
                         "int main(void) { int x = 10; (x -= 1) ? (x /= 2) : 0; return x == 4; }"));
}

TEST_F(BookTest, Chapter6_PostfixIf)
{
    EXPECT_EQ(
        "1\n",
        CompileAndRunBook(
            "int main(void) { int a = 0; if (a--) return 0; else if (a--) return 1; return 0; }"));
}

TEST_F(BookTest, Chapter6_PostfixInTernary)
{
    EXPECT_EQ("9\n",
              CompileAndRunBook("int main(void) { int x = 10; x - 10 ? 0 : x--; return x; }"));
}

TEST_F(BookTest, Chapter6_PrefixIf)
{
    EXPECT_EQ(
        "1\n",
        CompileAndRunBook(
            "int main(void) { int a = -1; if (++a) return 0; else if (++a) return 1; return 0; }"));
}

TEST_F(BookTest, Chapter6_PrefixInTernary)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { int a = 0; return (++a ? ++a : 0); }"));
}

TEST_F(BookTest, Chapter6_UnusedLabel)
{
    EXPECT_EQ("0\n", CompileAndRunBook("int main(void) { unused: return 0; }"));
}

TEST_F(BookTest, Chapter6_WhitespaceAfterLabel)
{
    EXPECT_EQ("1\n", CompileAndRunBook(
                         "int main(void) { goto label2; return 0; label1 : label2 : return 1; }"));
}
