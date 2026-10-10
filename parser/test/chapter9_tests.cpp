//
// Chapter 9 — Functions: invalid parser input.
// Imported from "Writing a C Compiler" (tests/chapter_9/invalid_parse).  Each
// program is lexically valid but cannot be parsed; the parser reports a fatal
// error.  Tests assert on a substring of the diagnostic.
//
// Reclassifications vs. the book (see semantic/chapter9_tests.cpp for the other
// direction):
//   * nested_function_definition is listed by the book under invalid_declarations,
//     but our parser rejects the nested body's '{' directly, so it lives here.
//   * call_non_identifier, function_returning_function,
//     initialize_function_as_variable and fun_decl_for_loop parse cleanly for us
//     (the C grammar permits the declarations) and are rejected by the type
//     checker, so they are semantic tests instead.
//
#include "fixture.h"

// --- invalid_parse ----------------------------------------------------------

// A parameter list must end with ')', not some other delimiter.
TEST_F(ParserTest, Chapter9_DeclWrongClosingDelim_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo(int x, int y} { return x + y; }

int main(void) { return 0;}
)")),
                 "expected '\\)' before '\\}'");
}

// An argument list must end with ')', not some other delimiter.
TEST_F(ParserTest, Chapter9_FuncallWrongClosingDelim_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo(int x, int y) {
    return x + y;
}

int main(void) { return foo(1, 2};}
)")),
                 "expected '\\)' before '\\}'");
}

// A function argument must be an expression, not a declaration.
TEST_F(ParserTest, Chapter9_FunctionCallDeclaration_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo(int a) {
    return 0;
}

int main(void) {
    return foo(int a);
}
)")),
                 "expected an expression before 'int'");
}

// A trailing comma is not permitted in a parameter list.
TEST_F(ParserTest, Chapter9_TrailingCommaDecl_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo(int a,) {
    return a + 1;
}

int main(void) {
    return foo(4);
}
)")),
                 "expected a type before '\\)'");
}

// A trailing comma is not permitted in an argument list.
TEST_F(ParserTest, Chapter9_TrailingComma_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo(int a, int b, int c) {
    return a + b + c;
}

int main(void) {
    return foo(1, 2, 3,);
}
)")),
                 "expected an expression before '\\)'");
}

// An unbalanced '(' in a parameter list is rejected at the body's '{'.
TEST_F(ParserTest, Chapter9_UnclosedParenDecl_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int foo(int a, int b {
    return 0;
}

int main(void) {
    return 0;
}
)")),
                 "expected '\\)' before '\\{'");
}

// A variable initializer is not permitted in a parameter list.
TEST_F(ParserTest, Chapter9_VarInitInParamList_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int bad_params(int a = 3) {
    return 1;
}

int main(void) {
    return 0;
}
)")),
                 "expected '\\)' before '='");
}

// A nested function *definition* is rejected: after the inner declarator the
// parser expects ';', not a function body '{' (book: invalid_declarations).
TEST_F(ParserTest, Chapter9_NestedFunctionDefinition_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"(int main(void) {
    int foo(void) {
        return 1;
    }
    return foo();
}
)")),
                 "expected ';' before '\\{'");
}
