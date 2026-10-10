//
// Chapter 10 — File-scope variables and storage-class specifiers: invalid
// parser input.  Imported from "Writing a C Compiler"
// (tests/chapter_10/invalid_parse + extra_credit).  Each program is lexically
// valid but cannot be parsed; the parser reports a fatal error.  Tests assert
// on a substring of the diagnostic.
//
// Reclassifications vs. the book (see semantic/chapter10_tests.cpp for the
// other direction):
//   * static_var_case is listed by the book under invalid_types/extra_credit,
//     but a case label requires a constant expression in our grammar, so the
//     parser rejects "case i:" directly (like ch8 non_constant_case).  It lives
//     here.
//
// New parser checks added for this chapter:
//   * "Multiple storage class specifiers" — a declaration may include at most
//     one storage-class keyword (C11 §6.7.1p2).
//   * "A function parameter cannot have a storage class" (C11 §6.7.6.3p2).
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// A function parameter cannot have a storage class (extern).
TEST_F(ParserTest, Chapter10_ExternParam_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int f(extern int i) {
    return i;
}

int main(void) {
    return f(1);
}
)")),
                 "a parameter cannot have a storage class");
}

// A function parameter cannot have a storage class (static).
TEST_F(ParserTest, Chapter10_StaticParam_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int f(static int i) {
    return i;
}

int main(void) {
    return f(1);
}
)")),
                 "a parameter cannot have a storage class");
}

// A function declarator must have a parameter list.
TEST_F(ParserTest, Chapter10_MissingParameterList_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int f {
    return 0
};

int main(void) {
    return 0;
}
)")),
                 "expected ';' before '\\}'");
}

// A declaration must have at least one type specifier.
TEST_F(ParserTest, Chapter10_MissingTypeSpecifier_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(static var = 0;

int main(void) {
    return var;
}
)")),
                 "expected a type before 'var'");
}

// A function declaration can't have multiple storage-class keywords.
TEST_F(ParserTest, Chapter10_MultiStorageClassFun_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(static int extern foo(void) {
    return 0;
}

int main(void) {
    return foo();
}
)")),
                 "more than one storage class in a declaration");
}

// A variable can't have more than one storage class.
TEST_F(ParserTest, Chapter10_MultiStorageClassVar_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    static extern int foo = 0;
    return foo;
}
)")),
                 "more than one storage class in a declaration");
}

// A declaration cannot include both static and extern specifiers.
TEST_F(ParserTest, Chapter10_StaticAndExtern_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(static extern int a;

int main(void) {
    return 0;
}
)")),
                 "more than one storage class in a declaration");
}

// --- invalid_parse / extra_credit ------------------------------------------

// The extern specifier cannot be applied to labels.
TEST_F(ParserTest, Chapter10_ExternLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    extern a:
    return 1;
}
)")),
                 "expected a type before 'a'");
}

// Labels cannot appear at file scope.
TEST_F(ParserTest, Chapter10_FileScopeLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(x:
int foo = 0;

int main(void) {
    return 0;
}
)")),
                 "expected a type before 'x'");
}

// The static specifier cannot be applied to labels.
TEST_F(ParserTest, Chapter10_StaticLabel_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    static a:
    return 1;
}
)")),
                 "expected a type before 'a'");
}

// --- reclassified from invalid_types/extra_credit ---------------------------

// A case label requires a constant expression; a static variable is not one,
// and our grammar rejects it at parse time.
TEST_F(ParserTest, Chapter10_StaticVarCase_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    static int i = 0;

    switch(0) {
        case i: return 0;
    }
    return 0;
}
)")),
                 "expected a constant expression");
}
