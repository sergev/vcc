//
// AArch64 homogeneous float aggregates and long double values: passed and returned in
// v registers, an element in each (AAPCS64), and the classification itself.
//
#include "aarch64_test.h"

// Three floats go in s0-s2, one member each; the result comes back the same way.
TEST_F(Aarch64Test, HfaInVRegisters)
{
    aarch64_frame_pointer = true; // slots at x29 offsets
    aarch64_peephole      = false; // the ABI, not its clean-up
    std::string code = Code(CompileToAarch64(R"(
struct v3 { float x, y, z; };
struct v3 f(struct v3 v);
float g(void) { struct v3 v = { 1, 2, 3 }; struct v3 r = f(v); return r.z; }
)"));
    EXPECT_NE(std::string::npos, code.find("ldr s0, [x29, #-")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr s1, [x29, #-")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr s2, [x29, #-")) << code;
    EXPECT_NE(std::string::npos, code.find("str s2, [x29, #-")) << code;
}

// A long double goes in a q register, whole.
TEST_F(Aarch64Test, LongDoubleInQRegister)
{
    aarch64_frame_pointer = true; // slots at x29 offsets
    aarch64_peephole      = false; // the ABI, not its clean-up
    std::string code = Code(CompileToAarch64(R"(
long double f(long double x);
long double g(long double y) { return f(y); }
)"));
    EXPECT_NE(std::string::npos, code.find("str q0, [x29, #-16]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr q0, [x29, #-16]\nbl f\n")) << code;
}

// Arguments: HFAs of each FP type, a union and an array member, an HFA that no longer
// fits v0-v7 (whole on the stack, and none after it in a v register), and HFA and long
// double results, both ways with clang.
TEST_F(Aarch64Test, RunHfaInteropWithClang)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    SKIP_IF_NO_AARCH64_CLANG();
    const char *common = R"(
struct f3 { float a, b, c; };
struct d4 { double a[2]; double b, c; };
struct q2 { long double a, b; };
union u { float f[2]; struct { float x, y; } s; };
struct mixed { float a; double b; };
long ld_part(struct q2 c, long double h);
long ours(struct f3 a, struct d4 b, struct q2 c, union u d, struct mixed e, struct d4 f,
          double g, long double h, int k);
long theirs(struct f3 a, struct d4 b, struct q2 c, union u d, struct mixed e, struct d4 f,
            double g, long double h, int k);
struct f3 our_f3(float k);
struct d4 our_d4(double k);
struct q2 our_q2(struct q2 k);
long double our_ld(long double k);
struct f3 their_f3(float k);
struct d4 their_d4(double k);
struct q2 their_q2(struct q2 k);
long double their_ld(long double k);
int call_ours(void);
)";
    // Both sides define one of each with this text, renamed.
    const char *defs   = R"(
long NAME(struct f3 a, struct d4 b, struct q2 c, union u d, struct mixed e, struct d4 f,
          double g, long double h, int k)
{
    return (long)(a.c * 10 + b.c + d.s.y + e.b + f.a[1] * 100 + g + k) + ld_part(c, h);
}
struct f3 PFX_f3(float k) { struct f3 r = { k, k + 1, k + 2 }; return r; }
struct d4 PFX_d4(double k) { struct d4 r = { { k, k + 1 }, k + 2, k + 3 }; return r; }
struct q2 PFX_q2(struct q2 k) { struct q2 r = { k.b, k.a }; return r; }
long double PFX_ld(long double k) { return k; }
)";
    const char *args   = R"(
    struct f3 a = { 1, 2, 3 };
    struct d4 b = { { 4, 5 }, 6, 7 };
    struct q2 c = { 8, 9 };
    union u d = { { 10, 11 } };
    struct mixed e = { 12, 13 };
    struct d4 f = { { 14, 15 }, 16, 17 };
    long expect = 30 + 7 + 11 + 13 + 1500 + 18 + 20 + 9 + 19000;
)";
    auto subst = [](std::string s, const std::string &name, const std::string &pfx) {
        for (size_t at; (at = s.find("NAME")) != std::string::npos;)
            s.replace(at, 4, name);
        for (size_t at; (at = s.find("PFX")) != std::string::npos;)
            s.replace(at, 3, pfx);
        return s;
    };
    std::string ours   = std::string(common) + subst(defs, "ours", "our") + R"(
int main(void)
{)" + args + R"(
    struct f3 r1 = their_f3(1);
    struct d4 r2 = their_d4(2);
    struct q2 r3 = their_q2(c);
    long double r4 = their_ld(c.b);
    return (theirs(a, b, c, d, e, f, 18, 19, 20) == expect)
         + 2 * (r1.c == 3 && r2.c == 5 && ld_part(r3, r4) == 8 + 9000)
         + 4 * call_ours();
})";
    std::string theirs = std::string(common) + subst(defs, "theirs", "their") + R"(
long ld_part(struct q2 c, long double h) { return (long)(c.b + h * 1000); }
int call_ours(void)
{)" + args + R"(
    return ours(a, b, c, d, e, f, 18, 19, 20) == expect && our_f3(1).c == 3 &&
           our_d4(2).c == 5 && our_q2(c).b == 8 && our_ld(6) == 6;
})";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(7, exit_status);
}
