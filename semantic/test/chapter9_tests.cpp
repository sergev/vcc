//
// Chapter 9 — Functions: semantic errors.
// Imported from "Writing a C Compiler" (tests/chapter_9/invalid_declarations,
// invalid_labels, invalid_types + extra_credit, plus four invalid_parse
// programs the C grammar permits but the type checker rejects).  Each program
// parses cleanly but fails type checking; tests assert on a substring of the
// fatal-error text.
//
// Reclassifications vs. the book:
//   * nested_function_definition is a *parse* error for us (the nested body's
//     '{' is rejected), so it lives in parser/chapter9_tests.cpp.
//   * call_non_identifier, function_returning_function,
//     initialize_function_as_variable and fun_decl_for_loop are listed by the
//     book under invalid_parse, but the C grammar permits these declarations, so
//     our parser accepts them and the type checker rejects them — they are here.
//   * call_variable_as_function: the book expects "x isn't a function"; for us
//     the local `int x = 0;` shadows the file-scope function `x` and is rejected
//     first by the permanent no-shadowing rule (external-vs-no-linkage clash).
//
#include "typecheck_fixture.h"

// --- invalid_declarations ---------------------------------------------------

// A function call is not an lvalue.
TEST_F(PipelineTest, Chapter9_AssignToFunCall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    x() = 1;
    return 0;
}
)"),
                 "expression is not assignable");
}

// Duplicate parameter names in a function prototype.
TEST_F(PipelineTest, Chapter9_DeclParamsWithSameName_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a, int a);

int main(void) {
    return foo(1, 2);
}
)"),
                 "Duplicate parameter name a");
}

// Duplicate parameter names in a function definition.
TEST_F(PipelineTest, Chapter9_ParamsWithSameName_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a, int a) {
    return a;
}

int main(void) {
    return foo(1, 2);
}
)"),
                 "Duplicate parameter name a");
}

// A function (external linkage) and a variable (no linkage) with the same name
// in one scope conflict.
TEST_F(PipelineTest, Chapter9_RedefineFunAsVar_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int foo(void);
    int foo = 1;
    return foo;
}
)"),
                 "Duplicate variable declaration foo");
}

// A function's parameter list and body share a scope; redeclaring 'a' is illegal.
TEST_F(PipelineTest, Chapter9_RedefineParameter_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a) {
    int a = 5;
    return a;
}

int main(void) {
    return foo(3);
}
)"),
                 "Duplicate variable declaration a");
}

// As above, in the other declaration order.
TEST_F(PipelineTest, Chapter9_RedefineVarAsFun_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int foo = 1;
    int foo(void);
    return foo;
}
)"),
                 "Duplicate variable declaration foo");
}

// A function must be declared before it is called.
TEST_F(PipelineTest, Chapter9_UndeclaredFun_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    return foo(3);
}

int foo(int a) {
    return 1;
}
)"),
                 "use of undeclared identifier 'foo'");
}

// Parameter names from an earlier declaration of foo are not in scope in the
// definition's body.
TEST_F(PipelineTest, Chapter9_WrongParameterNames_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a);

int main(void) {
    return foo(3);
}

int foo(int x) {
    return a;
}
)"),
                 "use of undeclared identifier 'a'");
}

// --- invalid_declarations / extra_credit ------------------------------------

// A label and a function are in separate namespaces; 'a()' calls undeclared 'a'.
TEST_F(PipelineTest, Chapter9_CallLabelAsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x = 1;
    a:
    x = x + 1;
    a();
    return x;
}
)"),
                 "use of undeclared identifier 'a'");
}

// A function call is not an lvalue, so it cannot be a compound-assignment target.
TEST_F(PipelineTest, Chapter9_CompoundAssignToFunCall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    x() += 1;
    return 0;
}
)"),
                 "expression is not assignable");
}

// A function call is not an lvalue, so it cannot be decremented.
TEST_F(PipelineTest, Chapter9_DecrementFunCall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    x()--;
}
)"),
                 "expression is not assignable");
}

// A function call is not an lvalue, so it cannot be incremented.
TEST_F(PipelineTest, Chapter9_IncrementFunCall_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    ++x();
}
)"),
                 "expression is not assignable");
}

// --- invalid_labels / extra_credit ------------------------------------------

// You cannot goto a label defined in another function.
TEST_F(PipelineTest, Chapter9_GotoCrossFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void) {
    label:
        return 0;
}

int main(void) {
    goto label;
    return 1;
}
)"),
                 "use of undeclared label 'label'");
}

// A function name cannot be used as a goto target.
TEST_F(PipelineTest, Chapter9_GotoFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void) {
    return 3;
}

int main(void) {
    goto foo;
    return 3;
}
)"),
                 "use of undeclared label 'foo'");
}

// --- invalid_types ----------------------------------------------------------

// A function designator cannot be assigned to an int variable.
TEST_F(PipelineTest, Chapter9_AssignFunToVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);
int main(void) {
    int a = 10;
    a = x;
    return 0;
}
)"),
                 "cannot convert '.*' to '.*' when");
}

// A function is not a modifiable lvalue, so it cannot be assigned to.
TEST_F(PipelineTest, Chapter9_AssignValueToFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int x(void);
    x = 3;
    return 0;
}
)"),
                 "is not assignable");
}

// A variable named like a function cannot shadow it (no-shadowing / linkage).
TEST_F(PipelineTest, Chapter9_CallVariableAsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    int x = 0;
    return x();
}
)"),
                 "Duplicate variable declaration x");
}

// A prototype and a definition of foo with different arity conflict.
TEST_F(PipelineTest, Chapter9_ConflictingFunctionDeclarations_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a);

int main(void) {
    return 5;
}

int foo(int a, int b) {
    return 4;
}
)"),
                 "Conflicting declarations for function foo");
}

// Two block-scope declarations of foo in different functions conflict.
TEST_F(PipelineTest, Chapter9_ConflictingLocalFunctionDeclaration_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int bar(void);

int main(void) {
    int foo(int a);
    return bar() + foo(1);
}

int bar(void) {
    int foo(int a, int b);
    return foo(1, 2);
}
)"),
                 "Conflicting declarations for function foo");
}

// A function designator is not an arithmetic operand for division.
TEST_F(PipelineTest, Chapter9_DivideByFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    int a = 10 / x;
    return 0;
}
)"),
                 "invalid operands to '[*/%]'");
}

// A function defined twice.
TEST_F(PipelineTest, Chapter9_MultipleFunctionDefinitions_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void){
    return 3;
}

int main(void) {
    return foo();
}

int foo(void){
    return 4;
}
)"),
                 "Defined function foo twice");
}

// A function defined twice, with an intervening local declaration of it.
TEST_F(PipelineTest, Chapter9_MultipleFunctionDefinitions2_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void){
    return 3;
}

int main(void) {
    int foo(void);
    return foo();
}

int foo(void){
    return 4;
}
)"),
                 "Defined function foo twice");
}

// --- valid-dir programs reclassified as negatives by the no-shadowing rule ---
//
// The book lists these four under valid/, but each shadows an enclosing name in
// a way the permanent no-shadowing design forbids.  A block-scope declaration
// (variable, function, or parameter) may not reuse a name already in scope.

// A nested function declaration shadows an enclosing local variable.
TEST_F(PipelineTest, Chapter9_FunctionShadowsVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int foo = 3;
    int bar = 4;
    if (foo + bar > 0) {
        int foo(void);
        bar = foo();
    }
    return foo + bar;
}

int foo(void) {
    return 8;
}
)"),
                 "Duplicate variable declaration");
}

// A nested local variable shadows an enclosing function declaration.
TEST_F(PipelineTest, Chapter9_VariableShadowsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int foo(void);

    int x = foo();
    if (x > 0) {
        int foo = 3;
        x = x + foo;
    }
    return x;
}

int foo(void) {
    return 4;
}
)"),
                 "Duplicate variable declaration");
}

// A parameter shadows a file-scope function of the same name.
TEST_F(PipelineTest, Chapter9_ParameterShadowsFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int a(void) {
    return 1;
}

int b(int a) {
    return a;
}

int main(void) {
    return a() + b(2);
}
)"),
                 "Duplicate variable declaration");
}

// A parameter shadows the function it belongs to.
TEST_F(PipelineTest, Chapter9_ParameterShadowsOwnFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int a(int a) {
    return a * 2;
}

int main(void) {
    return a(1);
}
)"),
                 "Duplicate variable declaration");
}

// foo takes two parameters but is called with one.
TEST_F(PipelineTest, Chapter9_TooFewArgs_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a, int b) {
    return a + 1;
}

int main(void) {
    return foo(1);
}
)"),
                 "too (many|few) arguments to function");
}

// foo takes one parameter but is called with two.
TEST_F(PipelineTest, Chapter9_TooManyArgs_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(int a) {
    return a + 1;
}

int main(void) {
    return foo(1, 2);
}
)"),
                 "too (many|few) arguments to function");
}

// --- invalid_types / extra_credit -------------------------------------------

// A function designator is not an integer operand for a shift.
TEST_F(PipelineTest, Chapter9_BitwiseOpFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    x >> 2;
    return 0;
}
)"),
                 "invalid operands to '");
}

// A function is not a modifiable lvalue for a compound assignment.
TEST_F(PipelineTest, Chapter9_CompoundAssignFunctionLhs_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    x += 3;
    return 0;
}
)"),
                 "is not assignable");
}

// A function designator is not a valid compound-assignment operand on the rhs.
TEST_F(PipelineTest, Chapter9_CompoundAssignFunctionRhs_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    int a = 3;
    a += x;
    return 0;
}
)"),
                 "invalid operands to '");
}

// A function name cannot be post-incremented.
TEST_F(PipelineTest, Chapter9_PostfixIncrFunName_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void) {
    x++;
    return 0;
}
)"),
                 "expression is not assignable");
}

// A function name cannot be pre-decremented.
TEST_F(PipelineTest, Chapter9_PrefixDecrFunName_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int x(void);

int main(void){
    --x;
    return 0;
}
)"),
                 "expression is not assignable");
}

// A function cannot be the controlling expression of a switch.
TEST_F(PipelineTest, Chapter9_SwitchOnFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    int f(void);
    switch (f)
        return 0;
}
)"),
                 "'switch' requires an integer value");
}

// --- reclassified from invalid_parse (grammar permits; type checker rejects) -

// You can only call a function, not a constant.
TEST_F(PipelineTest, Chapter9_CallNonIdentifier_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    return 1();
}
)"),
                 "not a function or function pointer");
}

// A function cannot return a function.
TEST_F(PipelineTest, Chapter9_FunctionReturningFunction_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void)(void);

int main(void) {
    return 0;
}
)"),
                 "function cannot return function type");
}

// A function declaration cannot have an initializer.
TEST_F(PipelineTest, Chapter9_InitializeFunctionAsVariable_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int foo(void) = 3;

int main(void) {
    return 0;
}
)"),
                 "Function declared with initializer");
}

// A function declaration is not permitted in a for-loop header.
TEST_F(PipelineTest, Chapter9_FunDeclForLoop_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(int main(void) {
    for (int f(void); ; ) {
        return 0;
    }
}
)"),
                 "'for' loop cannot");
}
