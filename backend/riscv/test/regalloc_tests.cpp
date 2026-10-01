//
// Register allocation: variables in argument registers in a function without calls,
// else in callee-saved registers saved only when used; copies coalesced, the rest
// spilled to their slots.
//
#include <regex>

#include "riscv_test.h"

// A function without calls keeps its values in the argument registers, a parameter
// in the one it arrives in: no frame at all.
TEST_F(RiscvTest, RegallocLeaf)
{
    EXPECT_EQ(R"(mul a1, a0, a1
add a0, a1, a0
ret
)",
              Code(CompileToRiscv("long f(long a, long b) { return a * b + a; }")));
}

// Across a call values need callee-saved registers, saved only when used.
TEST_F(RiscvTest, RegallocCalls)
{
    EXPECT_EQ(R"(addi sp, sp, -16
sd ra, 8(sp)
sd s0, 0(sp)
addi s0, sp, 16
addi sp, sp, -16
sd s1, -24(s0)
sd s2, -32(s0)
mv s1, a0
mv s2, a1
mv a0, s1
call g
mv s1, a0
mul s1, s1, s2
mv a0, s1
ld s1, -24(s0)
ld s2, -32(s0)
addi sp, s0, -16
ld ra, 8(sp)
ld s0, 0(sp)
addi sp, sp, 16
ret
)",
              Code(CompileToRiscv("long g(long x);\n"
                                  "long f(long a, long b) { return g(a) * b; }")));
}

TEST_F(RiscvTest, RegallocFloat)
{
    EXPECT_EQ("fmul.d fa1, fa0, fa1\nfadd.d fa0, fa1, fa0\nret\n",
              Code(CompileToRiscv("double f(double x, double y) { return x * y + x; }")));
}

// With a call: fs registers, saved with fsd.
TEST_F(RiscvTest, RegallocFloatCalls)
{
    std::string s = Code(CompileToRiscv(
        "double h(double);\ndouble g(double x) { double y = h(x); return y + x; }"));
    EXPECT_NE(std::string::npos, s.find("fsd fs0, -24(s0)\n")) << s;
    EXPECT_NE(std::string::npos, s.find("fld fs0, -24(s0)\n")) << s;
}

// Parameters rotated through each other in a loop, all in argument registers.
TEST_F(RiscvTest, RegallocLeafRun)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
long rot(long a, long b, long c, int n)
{
    for (int i = 0; i < n; i++) {
        long t = a;
        a = b;
        b = c;
        c = t;
    }
    return a * 100 + b * 10 + c;
}
double mix(double x, long k, double y) { return x * k - y; }
int main(void)
{
    if (rot(1, 2, 3, 4) != 231) return 1;
    if (mix(1.5, 4, 0.5) != 5.5) return 2;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Unoptimized TAC copies each value through temporaries; coalescing gives the ends of
// each copy one register, so no register-to-register move is left in the body.
TEST_F(RiscvTest, RegallocCoalesce)
{
    DisableOptimization();
    std::string s = Code(CompileToRiscv("long f(long a) { long b = a; long c = b; return c + 1; }"));
    EXPECT_EQ(std::string::npos, s.find("mv ")) << s;
}

// More values live at once than there are registers: some stay in slots.
static std::string SpillSource()
{
    std::string src = "long f(long x);\nint main(void) {\n";
    for (int i = 0; i < 20; i++)
        src += "    long v" + std::to_string(i) + " = f(" + std::to_string(i) + ");\n";
    src += "    long s = 0;\n";
    for (int i = 0; i < 20; i++)
        src += "    s = s * 3 + v" + std::to_string(i) + ";\n";
    return src + "    return s % 251;\n}\nlong f(long x) { return x * x + 1; }\n";
}

TEST_F(RiscvTest, RegallocSpillCode)
{
    std::string s = Code(CompileToRiscv(SpillSource().c_str()));
    EXPECT_NE(std::string::npos, s.find("sd s11, ")) << s;
    EXPECT_TRUE(std::regex_search(s, std::regex("sd a0, -[0-9]+\\(s0\\)\n"))) << s;
}

TEST_F(RiscvTest, RegallocSpill)
{
    SKIP_IF_NO_RISCV_TOOLS();
    long v = 0;
    for (long i = 0; i < 20; i++)
        v = v * 3 + i * i + 1;
    EXPECT_EQ("", CompileAndRunRiscv(SpillSource()));
    EXPECT_EQ(v % 251, exit_status);
}

// Values in callee-saved registers survive calls, including calls to clang code.
TEST_F(RiscvTest, RegallocAcrossCalls)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(R"(
long sq(long x);
double half(double d);
int main(void)
{
    long s = 0;
    double d = 0;
    for (long i = 1; i <= 10; i++) {
        s += sq(i);
        d += half(i);
    }
    return s == 385 && d == 27.5 ? 0 : 1;
}
)",
                                         R"(
long sq(long x) { long a = x, b = x; return a * b; }
double half(double d) { double h = 0.5; return d * h; }
)"));
    EXPECT_EQ(0, exit_status);
}

// A char in a register keeps the form a store and reload would give it.
TEST_F(RiscvTest, RegallocNarrow)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
signed char sc(unsigned char u) { return (signed char)u; }
int main(void)
{
    signed char c = 127;
    unsigned char u = 255;
    unsigned short h = 65535;
    c++;
    u++;
    h += 2;
    if (c != -128) return 1;
    if (u != 0) return 2;
    if (h != 1) return 3;
    if (sc(250) != -6) return 4;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}
