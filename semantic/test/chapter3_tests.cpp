//
// Chapter 3 — Binary Operators: semantic errors.
// Imported from "Writing a C Compiler" (tests/chapter_3/invalid_parse).
//
// The book classifies malformed_paren.c as a *parse* error because its grammar
// only allows a call on an identifier.  Our parser is more permissive — it parses
// a call on any postfix expression — so "2 (- 3)" parses cleanly and the error
// surfaces in the type checker as "called object is not a function".
//
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "typecheck_fixture.h"

// fatal_error() is defined once for the whole semantic-tests binary in
// typecheck_tests.cpp; the compiler libraries call it.

// return 2 (- 3); — calling an integer constant as if it were a function.
TEST_F(PipelineTest, Chapter3_MalformedParen_Neg)
{
    EXPECT_DEATH(RunPipeline("int main(void) {\n    return 2 (- 3);\n}\n"),
                 "is not a function or function pointer");
}
