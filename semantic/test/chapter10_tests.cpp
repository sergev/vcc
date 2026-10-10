//
// Chapter 10 — File-scope variables and storage-class specifiers: semantic
// errors.  Imported from "Writing a C Compiler"
// (tests/chapter_10/invalid_declarations, invalid_labels, invalid_types +
// extra_credit).  Each program parses cleanly but fails type checking; tests
// assert on a substring of the fatal-error text.
//
// New semantic checks added for this chapter:
//   * "Identifier ... declared both with and without linkage" — a block-scope
//     extern that follows a same-scope no-linkage declaration (a local or a
//     parameter) conflicts (C11 §6.7p3).
//   * a static local that redeclares a same-scope name → "Duplicate variable
//     declaration".
//   * "Block-scope function declaration cannot be static" (C11 §6.7.1p7).
//   * "Static initializer is not a constant" (C11 §6.7.9p4) — replaces the
//     previous cryptic "Unsupported initializer" message.
//
// Reclassifications vs. the book (see parser/chapter10_tests.cpp for the other
// direction):
//   * static_var_case is rejected by our parser ("Expected constant
//     expression"), so it lives in the parser file.
//   * conflicting_variable_linkage_2 is caught by our no-shadowing rule: the
//     inner "extern int x" shadows the enclosing local "int x = 3" (an
//     external-vs-no-linkage clash), which we report as "declared both with and
//     without linkage" rather than as the file-scope linkage conflict the book
//     intends.
//
#include "typecheck_fixture.h"

// --- invalid_declarations ---------------------------------------------------

// Two same-scope declarations of the same identifier with no linkage conflict.
TEST_F(PipelineTest, Chapter10_ConflictingLocalDeclarations_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 1;
    static int x;
    return x;
}
)"),
                 "Duplicate variable declaration x");
}

// An extern declaration cannot follow a same-scope local with no linkage.
TEST_F(PipelineTest, Chapter10_ExternFollowsLocalVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 3;
    extern int x;
    return x;
}
)"),
                 "declared both with and without linkage");
}

// A block-scope static has no linkage, so a following extern conflicts.
TEST_F(PipelineTest, Chapter10_ExternFollowsStaticLocalVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    static int x  = 0;
    extern int x;
    return x;
}
)"),
                 "linkage");
}

// A no-linkage local cannot follow a same-scope extern of the same name.
TEST_F(PipelineTest, Chapter10_LocalVarFollowsExtern_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int i = 10;

int main(void) {
    extern int i;
    int i;
    return i;
}
)"),
                 "Duplicate variable declaration i");
}

// A block-scope extern declaration does not leak past its block; the use of
// 'a' after the block is out of scope.
TEST_F(PipelineTest, Chapter10_OutOfScopeExternVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    {
        extern int a;
    }
    return a;
}

int a = 1;
)"),
                 "use of undeclared identifier");
}

// An extern cannot redefine a parameter (same scope, no linkage) of the same name.
TEST_F(PipelineTest, Chapter10_RedefineParamAsIdentifierWithLinkage_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int f(int i) {
    extern int i;
    return i;
}

int main(void) {
    return 0;
}
)"),
                 "declared both with and without linkage");
}

// A file-scope variable must be declared before it is used.
TEST_F(PipelineTest, Chapter10_UndeclaredGlobalVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    return x;
}

int x = 0;
)"),
                 "use of undeclared identifier 'x'");
}

// --- invalid_labels / extra_credit -----------------------------------------

// A goto statement can only target a label, not a variable.
TEST_F(PipelineTest, Chapter10_GotoGlobalVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x = 10;

int main(void) {
    goto x;
    return 0;
}
)"),
                 "use of undeclared label 'x'");
}

// --- invalid_types ----------------------------------------------------------

// A function with external linkage cannot be redefined with internal linkage.
TEST_F(PipelineTest, Chapter10_ConflictingFunctionLinkage_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void);

int main(void) {
    return foo();
}

static int foo(void) {
    return 0;
}
)"),
                 "Static function declaration follows non-static");
}

// As above, with the non-static declaration at block scope.
TEST_F(PipelineTest, Chapter10_ConflictingFunctionLinkage2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int foo(void);
    return foo();
}

static int foo(void) {
    return 0;
}
)"),
                 "Static function declaration follows non-static");
}

// A file-scope variable cannot be defined twice.
TEST_F(PipelineTest, Chapter10_ConflictingGlobalDefinitions_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo = 3;

int main(void) {
    return 0;
}

int foo = 4;
)"),
                 "Conflicting global variable definition");
}

// Internal- and external-linkage declarations of the same file-scope variable
// conflict.
TEST_F(PipelineTest, Chapter10_ConflictingVariableLinkage_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(static int foo;

int main(void) {
    return foo;
}

int foo = 3;
)"),
                 "Conflicting variable linkage");
}

// Caught by no-shadowing: the inner extern shadows the enclosing local x.
TEST_F(PipelineTest, Chapter10_ConflictingVariableLinkage2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 3;
    {
        extern int x;
    }
    return x;
}

static int x = 10;
)"),
                 "declared both with and without linkage");
}

// A for-loop counter cannot have a storage class (extern).
TEST_F(PipelineTest, Chapter10_ExternForLoopCounter_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    for (extern int i = 0; i < 10; i = i + 1) {
        x = x + 1;
    }
    return x;
}
)"),
                 "a declaration in a 'for' loop cannot have a storage class");
}

// An extern variable cannot have an initializer.
TEST_F(PipelineTest, Chapter10_ExternVariableInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    extern int i = 0;
    return i;
}
)"),
                 "Initializer on local extern declaration");
}

// A file-scope variable with static storage must have a constant initializer.
TEST_F(PipelineTest, Chapter10_NonConstantStaticInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int a = 10;
int b = 1 + a;

int main(void) {
    return b;
}
)"),
                 "Static initializer is not a constant");
}

// A static local variable must have a constant initializer.
TEST_F(PipelineTest, Chapter10_NonConstantStaticLocalInitializer_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int a = 1;
    static int b = a * 2;
    return b;
}
)"),
                 "Static initializer is not a constant");
}

// A file-scope variable cannot be redeclared as a function.
TEST_F(PipelineTest, Chapter10_RedeclareFileScopeVarAsFun_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo = 10;

int main(void) {
    int foo(void);
    return 0;
}
)"),
                 "Duplicate variable declaration foo");
}

// A function cannot be redeclared as a file-scope variable.
TEST_F(PipelineTest, Chapter10_RedeclareFunAsFileScopeVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void);

int foo;

int main(void) {
    return 0;
}
)"),
                 "Variable foo redeclared with different type");
}

// A function cannot be redeclared as a variable with extern linkage.
TEST_F(PipelineTest, Chapter10_RedeclareFunAsVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void) {
    return 0;
}

int main(void) {
    extern int foo;
    return 0;
}
)"),
                 "Variable foo redeclared with different type");
}

// A block-scope function declaration cannot have static storage class.
TEST_F(PipelineTest, Chapter10_StaticBlockScopeFunctionDeclaration_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    static int foo(void);
    return foo();
}

static int foo(void) {
    return 0;
}
)"),
                 "Block-scope function declaration cannot be static");
}

// A for-loop counter cannot have a storage class (static).
TEST_F(PipelineTest, Chapter10_StaticForLoopCounter_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 0;
    for (static int i = 0; i < 10; i = i + 1) {
        x = x + 1;
    }
    return x;
}
)"),
                 "a declaration in a 'for' loop cannot have a storage class");
}

// A file-scope variable cannot be called as a function.
TEST_F(PipelineTest, Chapter10_UseFileScopeVariableAsFun_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(extern int foo;

int main(void) {
    return foo();
}
)"),
                 "is not a function or function pointer");
}
