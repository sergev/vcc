//
// The wording of the type checker's diagnostics: the names and types they give,
// in the style of docs/Technical_Reference.md, "Diagnostics".
//
#include "typecheck_fixture.h"

// va_start (__va_start(&ap) on the targets that have it) in a function without '...'.
TEST_F(PipelineTest, VaStartInFixedArgumentFunction)
{
    EXPECT_DEATH(RunPipeline(R"SRC(
void __va_start(void *ap);
int f(int n)
{
    char *ap;
    __va_start(&ap);
    return n;
}
)SRC"),
                 "'va_start' used in a function with fixed arguments");
}

TEST_F(PipelineTest, VaStartInVariadicFunction)
{
    RunPipeline(R"SRC(
void __va_start(void *ap);
int f(int n, ...)
{
    char *ap;
    __va_start(&ap);
    return n;
}
)SRC");
}

// C11 §6.5.16p2, §6.5.2.4p1: the operand of an assignment, ++ or -- is a modifiable lvalue.
TEST_F(PipelineTest, AssignToConstVariable)
{
    EXPECT_DEATH(RunPipeline("int main(void) { const int a = 1; a = 2; return a; }"),
                 "cannot assign to variable 'a' with const-qualified type 'const int'");
}

TEST_F(PipelineTest, IncrementConstVariable)
{
    EXPECT_DEATH(RunPipeline("int main(void) { const int a = 1; a++; return a; }"),
                 "cannot assign to variable 'a' with const-qualified type 'const int'");
}

TEST_F(PipelineTest, AssignThroughPointerToConst)
{
    EXPECT_DEATH(RunPipeline("int f(const int *p) { *p = 1; return 0; }"),
                 "cannot assign to a read-only location of type 'const int'");
}

TEST_F(PipelineTest, AssignToMemberOfConstStruct)
{
    EXPECT_DEATH(RunPipeline("struct S { int x; };\n"
                             "int f(const struct S *p) { p->x = 1; return 0; }"),
                 "cannot assign to a read-only location");
    EXPECT_DEATH(RunPipeline("struct S { int x; };\n"
                             "int main(void) { const struct S s = { 1 }; s.x = 2; return 0; }"),
                 "cannot assign to a read-only location");
}

TEST_F(PipelineTest, AssignToConstTypedef)
{
    EXPECT_DEATH(RunPipeline("typedef const int CI;\n"
                             "int main(void) { CI a = 1; a += 1; return 0; }"),
                 "cannot assign to variable 'a' with const-qualified type 'CI'");
}

// A const pointer to a modifiable object, and a copy of a const value, are fine.
TEST_F(PipelineTest, ModifiableThroughConstPointer)
{
    RunPipeline("int f(int *const p) { *p = 1; return 0; }");
}

TEST_F(PipelineTest, ModifiableCopyOfConst)
{
    RunPipeline("int main(void) { const int a = 1; int b = a; b = 2; return b; }");
}

// The messages name the operator and the types of its operands.
TEST_F(PipelineTest, InvalidOperandsNamed)
{
    EXPECT_DEATH(RunPipeline("int main(void) { int *p = 0; float f = 1; return p + f; }"),
                 "invalid operands to '\\+' \\('int \\*' and 'float'\\)");
}

TEST_F(PipelineTest, ArgumentCountNamed)
{
    EXPECT_DEATH(RunPipeline("int f(int);\nint main(void) { return f(1, 2); }"),
                 "too many arguments to function 'f' \\(expected 1, have 2\\)");
}

TEST_F(PipelineTest, ConversionNamesContext)
{
    EXPECT_DEATH(RunPipeline("int f(char *);\nint main(void) { return f(3.0); }"),
                 "cannot convert 'double' to 'char \\*' when passing argument 1 of 'f'");
}

TEST_F(PipelineTest, ShadowingExplained)
{
    EXPECT_DEATH(RunPipeline("int main(void) { int x = 1; { int x = 2; } return x; }"),
                 "declaration of 'x' shadows an earlier one, which is not allowed");
}

// The parameters and the outermost block of the body are one scope (C11 §6.2.1p4).
TEST_F(PipelineTest, ParameterRedefinedInBody)
{
    EXPECT_DEATH(RunPipeline("int f(int a) { int a; return a; }"), "redefinition of 'a'");
}
