//
// AArch64 frame: slots below x29, copies through scratch registers, large frames.
//
#include <regex>

#include "aarch64_test.h"

// A local in a slot: stored, then loaded for the return.
TEST_F(Aarch64Test, LocalSlot)
{
    NaiveSelection();
    DisableOptimization();
    EXPECT_EQ(R"(stp x29, x30, [sp, #-16]!
mov x29, sp
sub sp, sp, #16
mov w9, #5
str w9, [x29, #-4]
ldr w0, [x29, #-4]
mov sp, x29
ldp x29, x30, [sp], #16
ret
)",
              Code(CompileToAarch64("int main(void) { int a = 5; return a; }")));
}

// Each width loads and stores as itself; a char by its signedness.
TEST_F(Aarch64Test, SlotWidths)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
long f(void) {
    signed char c = -1; unsigned char u = 255;
    short s = -2; unsigned short us = 2;
    long l = 7; long m = l; c = c; u = u; s = s;
    us = us;
    return m;
})"));
    for (const char *s : { "strb w9", "ldrsb w9", "ldrb w9", "strh w9", "ldrsh w9", "ldrh w9",
                           "str x9", "ldr x9", "ldr x0" })
        EXPECT_NE(std::string::npos, code.find(s)) << s << " in\n" << code;
}

// Offsets beyond ldur's reach go through ip0: sub with lsl #12 and a remainder.
TEST_F(Aarch64Test, LargeFrameOffsets)
{
    NaiveSelection();
    DisableOptimization();
    std::string src = "long f(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    long v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    return v599;\n}\n";
    std::string code = Code(CompileToAarch64(src.c_str()));
    EXPECT_NE(std::string::npos, code.find(", lsl #12\nsub sp, sp, #")) << code;
    EXPECT_NE(std::string::npos, code.find("sub x16, x29, #1, lsl #12\n")) << code;
    EXPECT_TRUE(std::regex_search(code, std::regex("\nsub x16, x29, #[0-9]+\n"))) << code;
    EXPECT_NE(std::string::npos, code.find("str x9, [x16]\n")) << code;
}

TEST_F(Aarch64Test, RunLocals)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    int a = 40; int b = a; long c = 2; unsigned char d = 200;
    b = b; c = c; d = d;
    return b;
})"));
    EXPECT_EQ(40, exit_status);
}

TEST_F(Aarch64Test, RunLargeFrame)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    std::string src = "int main(void) {\n"; // over 4 KiB of slots
    for (int i = 0; i < 1200; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    v0 = v1199;\n    return v0;\n}\n";
    EXPECT_EQ("", CompileAndRunAarch64(src));
    EXPECT_EQ(1199 & 255, exit_status);
}

// Addressed from sp, x30 takes the record's place, a lone saved register goes beside
// it, and the save at sp + 0 lowers sp itself (its restore raising it): no sub, no add.
TEST_F(Aarch64Test, FrameSpX30Alone)
{
    EXPECT_EQ(R"(str x30, [sp, #-16]!
mov w0, w0
bl g
add w0, w0, #1
ldr x30, [sp], #16
ret
)",
              Code(CompileToAarch64("int g(int); int f(int a) { return g(a) + 1; }")));
}
TEST_F(Aarch64Test, FrameSpX30Paired)
{
    EXPECT_EQ(R"(stp x30, x19, [sp, #-16]!
mov w0, w0
mov w19, w1
bl g
add w0, w0, w19
ldp x30, x19, [sp], #16
ret
)",
              Code(CompileToAarch64("int g(int); int f(int a, int b) { int x = g(a); return x + b; }")));
}
TEST_F(Aarch64Test, FrameSpWithSlots)
{
    EXPECT_EQ(R"(sub sp, sp, #32
str x30, [sp, #16]
str w0, [sp]
add x0, sp, #0
bl h
ldr w0, [sp, #4]
ldr x30, [sp, #16]
add sp, sp, #32
ret
)",
              Code(CompileToAarch64(
                  "void h(int *); int f(int a) { int v[4]; v[0] = a; h(v); return v[1]; }")));
}

// The frame layouts run: x30 alone, beside a lone register, with pairs, with slots,
// with a saved d register.
TEST_F(Aarch64Test, RunFrameLayouts)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
long id(long x) { return x; }
double did(double x) { return x; }
void fill(int *v) { v[1] = 5; }
long one(long a) { return id(a) + 1; }
long lone(long a, long b) { long x = id(a); return x + b; }
long many(long a, long b, long c, long d)
{
    long x = id(a), y = id(b);
    return x * 1000 + y * 100 + c * 10 + d + id(c);
}
double mixed(double a, double b, long c, long d, long e)
{
    double x = did(a);
    long y = id(c);
    return x + b + a + (double)(y + d + e + c);
}
int slots(int a) { int v[4]; v[0] = a; fill(v); return v[0] + v[1]; }
double dsum(double a) { return did(a) + a; }
int main(void)
{
    int ok = one(1) == 2 && lone(3, 4) == 7 && many(1, 2, 3, 4) == 1237 && slots(9) == 14 &&
             dsum(1.5) == 3.0 && mixed(1, 2, 3, 4, 5) == 19.0;
    return ok ? 42 : 1;
})"));
    EXPECT_EQ(42, exit_status);
}

// Saved registers pair within their file, a lone general one beside x30; a value
// copied away before a call and back is not copied back.
TEST_F(Aarch64Test, FrameSpFpPairs)
{
    EXPECT_EQ(R"(stp d8, d9, [sp, #-32]!
str x30, [sp, #16]
fmov d9, d0
fmov d8, d1
bl g
fadd d0, d0, d8
fadd d0, d0, d9
ldr x30, [sp, #16]
ldp d8, d9, [sp], #32
ret
)",
              Code(CompileToAarch64(
                  "double g(double); double f(double a, double b) { double x = g(a); return x + b + a; }")));
}
TEST_F(Aarch64Test, FrameSpMixedSaves)
{
    std::string code = Code(CompileToAarch64(R"(double g(double);
long h(long);
double f(double a, double b, long c, long d, long e)
{
    double x = g(a);
    long y = h(c);
    return x + b + a + (double)(y + d + e + c);
}
)"));
    EXPECT_NE(std::string::npos, code.find(R"(str d10, [sp, #-64]!
stp d8, d9, [sp, #16]
stp x19, x20, [sp, #32]
stp x30, x21, [sp, #48]
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldp d8, d9, [sp, #16]
ldp x19, x20, [sp, #32]
ldp x30, x21, [sp, #48]
ldr d10, [sp], #64
ret
)")) << code;
}
