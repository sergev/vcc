//
// ARM32 homogeneous FP aggregates: 1-4 members of one FP type (long double counting as
// double) in s or d registers, back-filled like FP scalars, on the stack once they no
// longer fit, closing the VFP registers; results in s0-s3/d0-d3.  Under the base
// standard (a variadic callee) they are ordinary composites.
//
#include "arm32_test.h"

// A float, three doubles in d1-d3, two floats in s8-s9, and a float back-filling s1.
TEST_F(Arm32Test, HfaArgumentsBackFill)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
struct f2 { float a, b; };
struct d3 { double a, b, c; };
double f(float a, struct d3 d, struct f2 e, float z);
double g(void) { struct d3 d = { 1, 2, 3 }; struct f2 e = { 4, 5 }; return f(0.5f, d, e, 9.0f); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(vmov.f32 s0, #0.5
vldr d1, [r11, #-24]
vldr d2, [r11, #-16]
vldr d3, [r11, #-8]
vldr s8, [r11, #-32]
vldr s9, [r11, #-28]
vmov.f32 s1, #9.0
bl f
)")) << code;
}

// The callee stores the registers into its slots; an HFA result comes back in d0-d2.
TEST_F(Arm32Test, HfaParametersAndResult)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
struct d3 { double a, b, c; };
struct f2 { float a, b; };
struct d3 r(struct f2 e, struct d3 d) { d.a = e.b; return d; }
double use(void) { struct f2 e = { 1, 2 }; struct d3 d = { 3, 4, 5 }; return r(e, d).c; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(vstr s0, [r11, #-8]
vstr s1, [r11, #-4]
vstr d1, [r11, #-32]
vstr d2, [r11, #-24]
vstr d3, [r11, #-16]
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(vldr d0, [r11, #-32]
vldr d1, [r11, #-24]
vldr d2, [r11, #-16]
sub sp, r11, #56
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(bl r
vstr d0, )")) << code;
}

// Not homogeneous: mixed float and double, an int member, or five members.
TEST_F(Arm32Test, NotHfaInCoreRegisters)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
struct m { float a; double b; };
struct f5 { float a[5]; };
int f(struct m x);
int g(struct f5 x);
int h(void) { struct m x = { 1, 2 }; struct f5 y = { { 0 } }; return f(x) + g(y); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr r3, [r11, #-4]
bl f
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldr r3, [r11, #-24]
bl g
)")) << code;
}

TEST(TacAbi, Aapcs32Class)
{
    Tac_Type f = { .kind = TAC_TYPE_FLOAT }, d = { .kind = TAC_TYPE_DOUBLE },
             ld = { .kind = TAC_TYPE_LONG_DOUBLE }, i = { .kind = TAC_TYPE_INT };
    EXPECT_EQ(4 * 8 + 1, tac_aapcs32_class(&f));
    EXPECT_EQ(8 * 8 + 1, tac_aapcs32_class(&ld));
    EXPECT_EQ(TAC_AAPCS32_CORE, tac_aapcs32_class(&i));
    // { double; long double } is homogeneous here, as AAPCS counts long double a double.
    Tac_Member m2 = { .name = const_cast<char *>("b"), .offset = 8, .type = &ld };
    Tac_Member m1 = { .next = &m2, .name = const_cast<char *>("a"), .offset = 0, .type = &d };
    Tac_Type s    = { .kind = TAC_TYPE_STRUCTURE };
    s.u.structure.members = &m1;
    s.u.structure.size    = 16;
    EXPECT_EQ(8 * 8 + 2, tac_aapcs32_class(&s));
    m2.type = &f;
    s.u.structure.size = 16; // padded after the float
    EXPECT_EQ(TAC_AAPCS32_CORE, tac_aapcs32_class(&s));
}

// HFAs of float and double, 1-4 members, as arguments and results both ways with clang:
// back-filling, an HFA that goes on the stack and closes the VFP registers, one with
// an array member, and a double/long double mix.
TEST_F(Arm32Test, RunHfaInteropWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    const char *decls = R"(
struct f1 { float a; };
struct f2 { float a, b; };
struct f3 { float a[3]; };
struct f4 { float a, b, c, d; };
struct d1 { double a; };
struct d2 { double a; long double b; };
struct d3 { double a, b, c; };
struct d4 { double a[2]; double b, c; };
)";
    std::string ours  = std::string(decls) + R"(
double theirs(float x, struct d3 a, struct f2 b, float y, struct f1 c, struct d1 d);
double theirs_stack(struct d4 a, struct d4 b, float x, struct f3 c, double y);
double theirs_mix(struct d2 a, struct f4 b, int k);
struct f3 theirs3(float k);
struct d4 theirs4(double k);
struct f1 theirs1(float k);
int call_ours(void);
double ours(float x, struct d3 a, struct f2 b, float y, struct f1 c, struct d1 d)
{
    return x + a.a + a.c + b.a + b.b + y + c.a + d.a;
}
double ours_stack(struct d4 a, struct d4 b, float x, struct f3 c, double y)
{
    return a.a[0] + a.c + b.a[1] + b.b + x + c.a[0] + c.a[2] + y;
}
double ours_mix(struct d2 a, struct f4 b, int k) { return a.a + a.b + b.a + b.d + k; }
struct f3 ours3(float k) { struct f3 r = { { k, k + 1, k + 2 } }; return r; }
struct d4 ours4(double k) { struct d4 r = { { k, k * 2 }, k * 3, k * 4 }; return r; }
struct f1 ours1(float k) { struct f1 r = { k * 2 }; return r; }
int main(void) {
    struct f1 c = { 6 };
    struct f2 b = { 3, 4 };
    struct f3 f = { { 9, 10, 11 } };
    struct f4 g = { 1, 2, 3, 4 };
    struct d1 d = { 7 };
    struct d2 m = { 0.5, 0.25 };
    struct d3 a = { 1, 0, 2 };
    struct d4 e = { { 1, 2 }, 3, 4 };
    struct f3 r3 = theirs3(20);
    struct d4 r4 = theirs4(1.5);
    struct f1 r1 = theirs1(3);
    return (theirs(0.5f, a, b, 5, c, d) == 0.5 + 1 + 2 + 3 + 4 + 5 + 6 + 7)
         + 2 * (theirs_stack(e, e, 8, f, 100) == 1 + 4 + 2 + 3 + 8 + 9 + 11 + 100)
         + 4 * (theirs_mix(m, g, 10) == 0.5 + 0.25 + 1 + 4 + 10)
         + 8 * (r3.a[0] == 20 && r3.a[2] == 22 && r4.a[1] == 3 && r4.c == 6 && r1.a == 6)
         + 16 * call_ours();
})";
    std::string theirs = std::string(decls) + R"(
double ours(float x, struct d3 a, struct f2 b, float y, struct f1 c, struct d1 d);
double ours_stack(struct d4 a, struct d4 b, float x, struct f3 c, double y);
double ours_mix(struct d2 a, struct f4 b, int k);
struct f3 ours3(float k);
struct d4 ours4(double k);
struct f1 ours1(float k);
double theirs(float x, struct d3 a, struct f2 b, float y, struct f1 c, struct d1 d)
{
    return x + a.a + a.c + b.a + b.b + y + c.a + d.a;
}
double theirs_stack(struct d4 a, struct d4 b, float x, struct f3 c, double y)
{
    return a.a[0] + a.c + b.a[1] + b.b + x + c.a[0] + c.a[2] + y;
}
double theirs_mix(struct d2 a, struct f4 b, int k) { return a.a + a.b + b.a + b.d + k; }
struct f3 theirs3(float k) { struct f3 r = { { k, k + 1, k + 2 } }; return r; }
struct d4 theirs4(double k) { struct d4 r = { { k, k * 2 }, k * 3, k * 4 }; return r; }
struct f1 theirs1(float k) { struct f1 r = { k * 2 }; return r; }
int call_ours(void)
{
    struct f1 c = { 6 };
    struct f2 b = { 3, 4 };
    struct f3 f = { { 9, 10, 11 } };
    struct f4 g = { 1, 2, 3, 4 };
    struct d1 d = { 7 };
    struct d2 m = { 0.5, 0.25 };
    struct d3 a = { 1, 0, 2 };
    struct d4 e = { { 1, 2 }, 3, 4 };
    struct f3 r3 = ours3(20);
    struct d4 r4 = ours4(1.5);
    struct f1 r1 = ours1(3);
    return ours(0.5f, a, b, 5, c, d) == 0.5 + 1 + 2 + 3 + 4 + 5 + 6 + 7 &&
           ours_stack(e, e, 8, f, 100) == 1 + 4 + 2 + 3 + 8 + 9 + 11 + 100 &&
           ours_mix(m, g, 10) == 0.5 + 0.25 + 1 + 4 + 10 && r3.a[1] == 21 && r4.b == 4.5 &&
           r1.a == 6;
}
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(31, exit_status);
}

// A variadic callee takes an HFA under the base standard, as a plain composite in core
// registers; clang's va_arg reads it from there.
TEST_F(Arm32Test, RunHfaToVariadicClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    std::string ours   = R"(
struct f2 { float a, b; };
struct d2 { double a, b; };
double vsum(int n, ...);
int main(void) {
    struct f2 x = { 1, 2 };
    struct d2 y = { 3, 4 };
    return vsum(2, x, y) == 10;
})";
    std::string theirs = R"(
#include <stdarg.h>
struct f2 { float a, b; };
struct d2 { double a, b; };
double vsum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    struct f2 x = va_arg(ap, struct f2);
    struct d2 y = va_arg(ap, struct d2);
    va_end(ap);
    return x.a + x.b + y.a + y.b;
}
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(1, exit_status);
}
