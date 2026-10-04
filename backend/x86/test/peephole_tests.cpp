//
// x86-64 peephole pass and compare-and-branch fusion: moves folded into their uses and
// results computed where they are moved, loads and addresses folded into the
// instructions that use them (sqrtsd included), reloads deleted, test for zero and masks, branch
// cleanup, and cmov for short triangles and diamonds.
//
#include "x86_test.h"

// A result is computed in the register it is moved to: the call's result is added to
// in rax, the parameter canonical form kept only where its upper half is read.
TEST_F(X86Test, ComputedInPlace)
{
    std::string code = Code(CompileToX86(R"(
int g(int);
int keep(int a, int b) { int x = g(a); return x + b; }
)"));
    EXPECT_NE(std::string::npos, code.find("call g\naddl %ebx, %eax\npopq %rbx\nret\n")) << code;
}

// Around a divide: the dividend and the result stay in rax.  b, allocated rbx as it
// is live across the divide, is read from rsi where it arrived, which the divide does
// not change; then nothing uses rbx, and it is not saved.  The parameters' 32-bit
// moves to themselves go, as nothing reads their upper halves.
TEST_F(X86Test, DivideInPlace)
{
    EXPECT_EQ("movl %edi, %eax\nmovl %edx, %r10d\ncltd\nidivl %r10d\naddl %esi, %eax\nret\n",
              Code(CompileToX86("int divc(int a, int b, int c) { return a / c + b; }")));
}

// A comparison feeding a branch is cmp and jcc, without setcc/movzbl/test; a load
// folds into the add, the address computation into the load.
TEST_F(X86Test, LoopFused)
{
    std::string code = Code(CompileToX86(R"(
long sum(long *p, int n)
{
    long s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)"));
    EXPECT_NE(std::string::npos, code.find(R"(cmpl %esi, %ecx
jge .LL0
movslq %ecx, %rdx
addq (%rdi,%rdx,8), %r8
addl $1, %ecx
jmp .L3
)")) << code;
    EXPECT_EQ(std::string::npos, code.find("set")) << code;
}

// An address computation folds into the memory operand: base, index, scale and
// displacement in one.
TEST_F(X86Test, AddressFolded)
{
    EXPECT_EQ("movl 4(%rdi,%rsi,8), %eax\nret\n", Code(CompileToX86(R"(
struct s { int a, b; };
int get(struct s *p, long i) { return p[i].b; }
)")));
}

// A load folds into sqrtsd even into its destination, which sqrtsd only writes.
TEST_F(X86Test, SqrtFromMemory)
{
    EXPECT_EQ("sqrtsd (%rdi), %xmm0\nret\n", Code(CompileToX86(R"(
#include <math.h>
double rootp(const double *p) { return sqrt(*p); }
)")));
}

// A mask computed only to be tested is test with the mask.
TEST_F(X86Test, MaskTest)
{
    std::string code = Code(CompileToX86("int bits(unsigned x) { if (x & 8) return 1; return 2; }"));
    EXPECT_EQ(0u, code.find("testl $8, %edi\nje .L2\n")) << code;
}

// The reload of what was just stored goes; a volatile one stays.
TEST_F(X86Test, ReloadDeleted)
{
    EXPECT_EQ("movl g(%rip), %eax\naddl $1, %eax\nmovl %eax, g(%rip)\nret\n",
              Code(CompileToX86("int g; int inc(void) { g = g + 1; return g; }")));
}

TEST_F(X86Test, VolatileReloadKept)
{
    EXPECT_EQ("movl %edi, g(%rip)\nmovl g(%rip), %eax\nret\n",
              Code(CompileToX86("volatile int g; int f(int x) { g = x; return g; }")));
}

// A triangle of one move is cmov.
TEST_F(X86Test, CmovTriangle)
{
    EXPECT_EQ("cmpl %esi, %edi\ncmovle %esi, %edi\nmovl %edi, %eax\nret\n",
              Code(CompileToX86("int max(int a, int b) { return a > b ? a : b; }")));
}

// A diamond of two moves: one made, the other conditional.
TEST_F(X86Test, CmovDiamond)
{
    EXPECT_EQ("testl %edi, %edi\ncmove %edx, %esi\nmovl %esi, %eax\nret\n",
              Code(CompileToX86(R"(
int pick(int c, int a, int b) { int x; if (c) x = a; else x = b; return x; }
)")));
}

// A constant is made conditional through r11, as cmov takes no immediate.
TEST_F(X86Test, CmovConstant)
{
    EXPECT_EQ("testl %edi, %edi\nmovl $0, %r11d\ncmovl %r11d, %edi\nmovl %edi, %eax\nret\n",
              Code(CompileToX86("int clampzero(int x) { return x < 0 ? 0 : x; }")));
}

// A diamond whose arm loads through a pointer: that load would be made on both paths,
// so neither arm may be the unconditional one.
TEST_F(X86Test, NoCmovLoadMadeUnconditional)
{
    std::string code = Code(CompileToX86("int f(int c, int *p) { return c ? *p : 5; }"));
    EXPECT_EQ(std::string::npos, code.find("cmov")) << code;
}

// The upper half of a parameter's register is read: its 32-bit move to itself stays,
// clearing it.
TEST_F(X86Test, UpperHalfKept)
{
    std::string code = Code(CompileToX86("double f(unsigned u) { return u; }"));
    EXPECT_EQ("movl %edi, %edi\ncvtsi2sdq %rdi, %xmm0\nret\n", code);
}

// cmov loads whatever the condition, so a load through a pointer stays a branch.
TEST_F(X86Test, NoCmovThroughPointer)
{
    std::string code = Code(CompileToX86("int f(int c, int *p, int b) { return c ? *p : b; }"));
    EXPECT_EQ(std::string::npos, code.find("cmov")) << code;
    EXPECT_NE(std::string::npos, code.find("je .L0\nmovl (%rsi), %edx\n")) << code;
}

// FP comparisons fused with their branch: `<` swaps the operands and takes jbe (true
// for unordered, so a NaN does not branch to the then-part); `==` needs two jumps for
// its inverse, `!=` two for the branch over its inverse.
TEST_F(X86Test, FpCompareBranch)
{
    EXPECT_EQ(0u, Code(CompileToX86("int lt(double a, double b) { if (a < b) return 1; return 2; }"))
                      .find("ucomisd %xmm0, %xmm1\njbe .L1\n"));
}

TEST_F(X86Test, FpEqualBranch)
{
    EXPECT_EQ(0u, Code(CompileToX86("int eq(double a, double b) { if (a == b) return 1; return 2; }"))
                      .find("ucomisd %xmm1, %xmm0\njne .L1\njp .L1\n"));
}

TEST_F(X86Test, FpNotEqualBranch)
{
    EXPECT_EQ(0u, Code(CompileToX86("int ne(double a, double b) { if (a != b) return 1; return 2; }"))
                      .find("ucomisd %xmm1, %xmm0\njp .Lx0\nje .L1\n"));
}

// The rewrites keep their meaning: selects of every kind, FP branches with NaN, folded
// loads and addresses, against the host's results.
TEST_F(X86Test, RunPeephole)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("7 -3 7 0 5 9\n1 0 0 1 1 0 0 1\n10 30 6\n", CompileAndRunX86(R"(
#include <stdio.h>

int max(int a, int b) { return a > b ? a : b; }
int min(int a, int b) { return a < b ? a : b; }
long lmax(long a, long b) { return a >= b ? a : b; }
int clamp0(int x) { return x < 0 ? 0 : x; }
int pick(int c, int a, int b) { int x; if (c) x = a; else x = b; return x; }
int deref(int c, int *p, int b) { return c ? *p : b; }

int lt(double a, double b) { if (a < b) return 1; return 0; }
int eq(double a, double b) { if (a == b) return 1; return 0; }
int ne(double a, double b) { if (a != b) return 1; return 0; }

struct s { int a, b; };
int get(struct s *p, long i) { return p[i].b; }
long sum(long *p, int n) { long s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }

int main(void)
{
    double nan = 0.0 / 0.0;
    int nine = 9;
    struct s v[3] = { { 1, 2 }, { 3, 6 }, { 5, 10 } };
    long w[4] = { 1, 2, 3, 4 };
    printf("%d %d %ld %d %d %d\n", max(7, -3), min(7, -3), lmax(7, 7), clamp0(-4),
           pick(0, 1, 5), deref(1, &nine, 0));
    printf("%d %d %d %d %d %d %d %d\n", lt(1, 2), lt(2, 1), lt(nan, 1), eq(1, 1), ne(1, 2),
           eq(nan, nan), ne(1, 1), ne(nan, nan));
    printf("%ld %d %d\n", sum(w, 4), get(v, 2) * 3, get(v, 1));
    return 0;
}
)"));
}
