//
// alloca on BESM-6 (Besm6_Calling_Conventions.md): the memory is the top of the stack, which grows
// upward; the result r15 as a void * fat pointer, then r15 raised by the words of the
// size; b/ret sets r15 back from r7, which gives the memory back.  Goldens in the three
// dialects, and programs run on the Unix path (b6sim).
//
#include "codegen_test.h"

static const char alloca_fn[] = R"(
void *__builtin_alloca(unsigned long);
int g(int *);
int f(int n)
{
    int *p = __builtin_alloca(10);
    int *q = __builtin_alloca(n);
    p[0] = n;
    q[0] = n;
    return g(p) + g(q);
}
)";

// A constant size takes ceil(n/6) words at once; a computed one n/6 + 1, the count
// through b/udiv, pushed and popped into C for the utm.
TEST_F(CodegenTest, AllocaUnix)
{
    std::string code = CompileToUnix(alloca_fn);
    EXPECT_NE(std::string::npos, code.find(R"(
    ita 15
    aox #0'64
  7 atx
 15 utm 2
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(
    ita 15
    aox #0'64
  7 atx 2
  6 xta
    xts #6
 13 vjm b$udiv
    xts
 15 wtc
 15 utm 1
)")) << code;
    EXPECT_NE(std::string::npos, code.find("    uj b$ret\n")) << code;
}

TEST_F(CodegenTest, AllocaMadlen)
{
    std::string code = CompileToMadlen(alloca_fn);
    EXPECT_NE(std::string::npos, code.find(",ita, 15\n")) << code;
    EXPECT_NE(std::string::npos, code.find("15 ,utm, 2\n")) << code;
    EXPECT_NE(std::string::npos, code.find(",call, b/udiv\n")) << code;
    EXPECT_NE(std::string::npos, code.find("15 ,wtc,\n")) << code;
    EXPECT_NE(std::string::npos, code.find("15 ,utm, 1\n")) << code;
}

TEST_F(CodegenTest, AllocaBemsh)
{
    std::string code = CompileToBemsh(alloca_fn);
    EXPECT_NE(std::string::npos, code.find(R"(
       счи 15
       или =в'6400000000000000'
       зп (7)
       слиа 2(15)
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(
       пв _udiv(13)
       счм
       мод (15)
       слиа 1(15)
)")) << code;
}

// The memory reused after each return: a thousand calls, each taking 100 words, would
// overrun the stack were it kept.  Constant and computed sizes, byte data through the
// void *, and recursion with an alloca at each level.
TEST_F(CodegenTest, UnixRunAllocaLoop)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    EXPECT_EQ("4950 4950 HELLO 55\n", CompileAndRunUnix(R"(
#include <alloca.h>
#include <stdio.h>
#include <string.h>
static int sum(int n)
{
    int *v = alloca(n * sizeof(int));
    for (int i = 0; i < n; i++)
        v[i] = i;
    int s = 0;
    for (int i = 0; i < n; i++)
        s += v[i];
    return s;
}
static int sum100(void)
{
    int *v = alloca(100 * sizeof(int));
    for (int i = 0; i < 100; i++)
        v[i] = i;
    int s = 0;
    for (int i = 0; i < 100; i++)
        s += v[i];
    return s;
}
static int rec(int n)
{
    int *v = alloca(sizeof(int));
    *v     = n;
    int r  = n ? rec(n - 1) : 0;
    return r + *v;
}
int main(void)
{
    int a = 0, b = 0;
    for (int k = 0; k < 1000; k++) {
        a = sum(100);
        b = sum100();
    }
    char *s = alloca(6);
    strcpy(s, "HELLO");
    printf("%d %d %s %d\n", a, b, s, rec(10));
    return 0;
}
)"));
}

// Arguments pushed after an alloca go above the memory and leave it intact, and a
// defer and an early return give it back as well.
TEST_F(CodegenTest, UnixRunAllocaArguments)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    EXPECT_EQ("78 abcdefghij 3\n", CompileAndRunUnix(R"(
#include <alloca.h>
#include <coro.h>
#include <stdio.h>
static int sum12(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j,
                 int k, int l)
{
    return a + b + c + d + e + f + g + h + i + j + k + l;
}
static int count;
static int early(int n)
{
    char *p = alloca(n);
    defer count++;
    p[0] = 1;
    if (n > 3)
        return p[0];
    return 0;
}
int main(void)
{
    char *p = alloca(11);
    for (int i = 0; i < 10; i++)
        p[i] = 'a' + i;
    p[10] = 0;
    int s = sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    for (int k = 0; k < 1000; k++)
        early(k % 8);
    early(1);
    early(5);
    printf("%d %s %d\n", s, p, count == 1002 ? 3 : count);
    return 0;
}
)"));
}

// The same memory on the Dubna paths, Madlen and Bemsh: a constant size, then calls
// taking a computed one in a loop above it, the sums printed.
static const char alloca_dubna[] = R"(
#include <alloca.h>
#include <stdio.h>
static int sum(int n)
{
    int *v = alloca(n * sizeof(int));
    for (int i = 0; i < n; i++)
        v[i] = i;
    int s = 0;
    for (int i = 0; i < n; i++)
        s += v[i];
    return s;
}
void program()
{
    int *w = alloca(3 * sizeof(int));
    w[0]   = 1;
    w[1]   = 2;
    w[2]   = 3;
    int a  = 0;
    for (int k = 0; k < 300; k++)
        a = sum(100);
    printf("%d %d\n", a, w[0] + w[1] + w[2]);
}
)";

TEST_F(CodegenTest, AllocaMadlenRun)
{
    EXPECT_EQ("4950 6\n", CompileAndRun(alloca_dubna));
}

TEST_F(CodegenTest, AllocaBemshRun)
{
    EXPECT_EQ("4950 6\n", CompileAndRunBemsh(alloca_dubna));
}
