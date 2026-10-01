//
// Chapter 9 — Functions: valid programs compiled and run on BESM-6.
// Imported from "Writing a C Compiler" (tests/chapter_9/valid + extra_credit +
// libraries).  Each program defines int main(void); b6sim --status prints its
// return value, and we compare program output against the value computed by host
// cc.
//
// Programs run unwrapped: b6sim --status appends main()'s return value after the
// program's own stdout.  The book's "register vs stack argument" distinction is
// x86-specific; BESM-6 passes every argument through the r6 parameter block, so
// many-argument calls run like any other.
//
// Reclassifications / divergences vs. the book:
//   * Four book-"valid" programs shadow an enclosing name and are rejected by
//     the permanent no-shadowing rule; they are semantic-negative tests in
//     semantic/chapter9_tests.cpp instead (function_shadows_variable,
//     variable_shadows_function, parameter_shadows_function,
//     parameter_shadows_own_function).
//   * The book's putchar() is not in our libc; the three programs that use it
//     call the existing libc routine putch() instead (declared "void putch(int)"
//     here).  putch emits a value as its packed bytes — a single small ASCII
//     code emits exactly one byte.
//
#include "book_test.h"

// --- arguments_in_registers -------------------------------------------------

// twice(3) == 6.
TEST_F(BookTest, Chapter9_SingleArg)
{
    EXPECT_EQ("6\n", CompileAndRunBook(R"(int twice(int x){
    return 2 * x;
}

int main(void) {
    return twice(3);
})"));
}

// Arguments are evaluated and passed left-to-right: sub(1+2, 1) == 2.
TEST_F(BookTest, Chapter9_ExpressionArgs)
{
    EXPECT_EQ("2\n", CompileAndRunBook(R"(int sub(int a, int b) {
    return a - b;
}

int main(void) {
    int sum = sub(1 + 2, 1);
    return sum;
})"));
}

// Recursive fibonacci: fib(6) == 8.
TEST_F(BookTest, Chapter9_Fibonacci)
{
    EXPECT_EQ("8\n", CompileAndRunBook(R"(int fib(int n) {
    if (n == 0 || n == 1) {
        return n;
    } else {
        return fib(n - 1) + fib(n - 2);
    }
}

int main(void) {
    int n = 6;
    return fib(n);
})"));
}

// A forward declaration may use different parameter names than the definition.
TEST_F(BookTest, Chapter9_ForwardDeclMultiArg)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int foo(int a, int b);

int main(void) {
    return foo(2, 1);
}

int foo(int x, int y){
    return x - y;
})"));
}

// A function declaration is its own scope, so the prototype parameter 'a' does
// not conflict with the local 'a'.  f(10) == 20.
TEST_F(BookTest, Chapter9_ParamShadowsLocalVar)
{
    EXPECT_EQ("20\n", CompileAndRunBook(R"(int main(void) {
    int a = 10;
    int f(int a);
    return f(a);
}

int f(int a) {
    return a * 2;
})"));
}

// Calling another function must not clobber this function's own arguments.
TEST_F(BookTest, Chapter9_ParametersArePreserved)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int g(int w, int x, int y, int z) {
    if (w == 2 && x == 4 && y == 6 && z == 8)
        return 1;
    return 0;
}

int f(int a, int b, int c, int d) {
    int result = g(a * 2, b * 2, c * 2, d * 2);
    return (result == 1 && a == 1 && b == 2 && c == 3 && d == 4);
}

int main(void) {
    return f(1, 2, 3, 4);
})"));
}

// A division (book: uses EDX on x86) must not clobber the third argument.
TEST_F(BookTest, Chapter9_DontClobberArgInDivision)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int x(int a, int b, int c, int d, int e, int f) {
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6;
}

int main(void) {
    int a = 4;
    return x(1, 2, 3, 4, 5, 24 / a);
})"));
}

// --- no_arguments -----------------------------------------------------------

// Forward declaration then later definition: foo() == 3.
TEST_F(BookTest, Chapter9_ForwardDecl)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int foo(void);

int main(void) {
    return foo();
}

int foo(void) {
    return 3;
})"));
}

// The same function may be declared more than once.
TEST_F(BookTest, Chapter9_MultipleDeclarations)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int main(void) {
    int f(void);
    int f(void);
    return f();
}

int f(void) {
    return 3;
})"));
}

// A void function may fall off the end; the caller ignores it and main returns 3.
// (A *non-void* function falling off the end is now a compile error — see the
// missing-return diagnostic in semantic/declarations.c.)
TEST_F(BookTest, Chapter9_NoReturnValue)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(void foo(void) {
    int x = 1;
}

int main(void) {
    foo();
    return 3;
})"));
}

// The call operator binds tighter than unary !: !three() == !3 == 0.
TEST_F(BookTest, Chapter9_Precedence)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int three(void) {
    return 3;
}

int main(void) {
    return !three();
})"));
}

// Several calls in one expression: foo() + bar() / 3 == 18 + 3 == 21.
TEST_F(BookTest, Chapter9_UseFunctionInExpression)
{
    EXPECT_EQ("21\n", CompileAndRunBook(R"(int bar(void) {
    return 9;
}

int foo(void) {
    return 2 * bar();
}

int main(void) {
    return foo() + bar() / 3;
})"));
}

// --- stack_arguments --------------------------------------------------------

// Eight arguments, all read in the callee.
TEST_F(BookTest, Chapter9_LotsOfArguments)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int foo(int a, int b, int c, int d, int e, int f, int g, int h) {
    return (a == 1 && b == 2 && c == 3 && d == 4 && e == 5
            && f == 6 && g == 7 && h == 8);
}

int main(void) {
    return foo(1, 2, 3, 4, 5, 6, 7, 8);
})"));
}

// Manage the callee side (read params) and caller side (call putch) together.
// Book uses putchar; we substitute libc putch.  foo prints 'A' and returns
// a + g == 1 + 7 == 8, so output is "A8\n".
TEST_F(BookTest, Chapter9_CallPutch)
{
    EXPECT_EQ("A8\n", CompileAndRunBook(R"(void putch(int c);
int foo(int a, int b, int c, int d, int e, int f, int g, int h) {
    putch(h);
    return a + g;
}

int main(void) {
    return foo(1, 2, 3, 4, 5, 6, 7, 65);
}
)"));
}

// The textbook test loops 10,000,000 times over a 15-argument call (far over the
// 10s ctest timeout on Dubna).  Shrunk to 100 iterations: each call returns
// l + o = ret + 15, so ret == 15 * 100 == 1500, still exercising the multi-arg
// call and stack-frame restore.
TEST_F(BookTest, Chapter9_TestForMemoryLeaks)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int lots_of_args(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l, int m, int n, int o) {
    return l + o;
}

int main(void) {
    int ret = 0;
    for (int i = 0; i < 100; i = i + 1) {
        ret = lots_of_args(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, ret, 13, 14, 15);
    }
    return ret == 1500;
})"));
}

// --- libraries (multi-file programs concatenated into one TU) ---------------

// addition_client.c + addition.c : add(1, 2) == 3.
TEST_F(BookTest, Chapter9_LibraryAddition)
{
    EXPECT_EQ("3\n", CompileAndRunBook(R"(int add(int x, int y);

int main(void) {
    return add(1, 2);
}

int add(int x, int y) {
    return x + y;
})"));
}

// many_args_client.c + many_args.c : x == 3, y == 589680, return 3 + 589680%256.
TEST_F(BookTest, Chapter9_LibraryManyArgs)
{
    EXPECT_EQ("115\n", CompileAndRunBook(R"(int fib(int a);

int multiply_many_args(int a, int b, int c, int d, int e, int f, int g, int h);

int main(void) {
    int x = fib(4);
    int seven = 7;
    int eight = fib(6);
    int y = multiply_many_args(x, 2, 3, 4, 5, 6, seven, eight);
    if (x != 3) {
        return 1;
    }
    if (y != 589680) {
        return 2;
    }
    return x + (y % 256);
}

int fib(int n) {
    if (n == 0 || n == 1) {
        return n;
    } else {
        return fib(n - 1) + fib(n - 2);
    }
}

int multiply_many_args(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a * b * c * d * e * f * fib(g) * fib(h);
})"));
}

// system_call_client.c + system_call.c : prints 'H' (= putch(70+2)), main == 0,
// so output is "H0\n".  Book's incr_and_print returns putchar(b+2); putch is
// void, so we ignore the result and return 0 (the caller ignores it anyway).
TEST_F(BookTest, Chapter9_LibrarySystemCall)
{
    EXPECT_EQ("H0\n", CompileAndRunBook(R"(void putch(int c);
int incr_and_print(int c);

int main(void) {
    incr_and_print(70);
    return 0;
}

int incr_and_print(int b) {
    putch(b + 2);
    return 0;
}
)"));
}

// no_function_calls/division_client.c + division.c : f(10,2,100,4) == 1.
TEST_F(BookTest, Chapter9_LibraryDivision)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int f(int a, int b, int c, int d);

int main(void) {
    return f(10, 2, 100, 4);
}

int f(int a, int b, int c, int d) {
    int x = a / b;
    if (a == 10 && b == 2 && c == 100 && d == 4 && x == 5)
        return 1;
    return 0;
})"));
}

// no_function_calls/local_stack_variables_client.c + local_stack_variables.c :
// the callee reads its stack arguments and updates one, returning 100.
TEST_F(BookTest, Chapter9_LibraryLocalStackVariables)
{
    EXPECT_EQ("100\n", CompileAndRunBook(R"(int f(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6,
    int stack1, int stack2, int stack3);

int main(void) {
    return f(1, 2, 3, 4, 5, 6, -1, -2, -3);
}

int f(int reg1, int reg2, int reg3, int reg4, int reg5, int reg6,
    int stack1, int stack2, int stack3) {
    int x = 10;
    if (reg1 == 1 && reg2 == 2 && reg3 == 3 && reg4 == 4 && reg5 == 5
        && reg6 == 6 && stack1 == -1 && stack2 == -2 && stack3 == -3
        && x == 10) {
        stack2 = 100;
        return stack2;
    }
    return 0;
})"));
}

// --- extra_credit -----------------------------------------------------------

// Hello, World!  Book uses putchar; we substitute libc putch.  main prints the
// text via putch and falls off the end, implicitly returning 0 (§5.1.2.2.3), so
// --status appends "0\n" after the text.  The BESM-6 GOST output charset renders
// lowercase Latin as Cyrillic, so we print uppercase letters ("HELLO, WORLD!").
TEST_F(BookTest, Chapter9_HelloWorld)
{
    EXPECT_EQ("HELLO, WORLD!\n0\n", CompileAndRunBook(R"(void putch(int c);

int main(void) {
    putch(72);
    putch(69);
    putch(76);
    putch(76);
    putch(79);
    putch(44);
    putch(32);
    putch(87);
    putch(79);
    putch(82);
    putch(76);
    putch(68);
    putch(33);
    putch(10);
}
)"));
}

// A function result may be the right operand of a compound assignment.
TEST_F(BookTest, Chapter9_CompoundAssignFunctionResult)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int foo(void) {
    return 2;
}

int main(void) {
    int x = 3;
    x -= foo();
    return x;
})"));
}

// A bitwise shift (book: uses ECX on x86) must not clobber the sixth argument.
TEST_F(BookTest, Chapter9_DontClobberArgInShift)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int x(int a, int b, int c, int d, int e, int f) {
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6;
}

int main(void) {
    int a = 4;
    return x(1, 2, 3, 4, 5, 24 >> (a / 2));
})"));
}

// The same label name may appear in multiple functions: main jumps to its own
// label, which calls foo, which jumps to its own label and returns 5.
TEST_F(BookTest, Chapter9_GotoLabelMultipleFunctions)
{
    EXPECT_EQ("5\n", CompileAndRunBook(R"(int foo(void) {
    goto label;
    return 0;
    label:
        return 5;
}

int main(void) {
    goto label;
    return 0;
    label:
        return foo();
})"));
}

// An identifier may be both a function name and a label in the same scope.
TEST_F(BookTest, Chapter9_GotoSharedName)
{
    EXPECT_EQ("1\n", CompileAndRunBook(R"(int foo(void) {
    goto foo;
    return 0;
    foo:
        return 1;
}

int main(void) {
    return foo();
})"));
}

// Labels in different functions must not collide after name mangling.
TEST_F(BookTest, Chapter9_LabelNamingScheme)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(int main(void) {
    _label:
    label_:
    return 0;
}

int main_(void) {
    label:
    return 0;
}

int _main(void) {
    label: return 0;
})"));
}
