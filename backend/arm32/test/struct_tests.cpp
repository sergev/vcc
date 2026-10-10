//
// ARM32 structs: member access, copies, and passing and returning them by value
// (AAPCS composites other than homogeneous FP aggregates, which hfa_tests.cpp covers).
//
#include "arm32_test.h"

// A 3-byte struct goes in a core register, assembled byte by byte (an object may end
// there); an int-sized one is a word.
TEST_F(Arm32Test, SmallStructInRegisters)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
struct s { char a, b, c; };
struct w { short a; char b; };
int f(struct s v, struct w x);
int g(void) { struct s v = { 1, 2, 3 }; struct w x = { 4, 5 }; return f(v, x); }
)"));
    EXPECT_NE(std::string::npos, code.find("ldrb r0, [r11, #")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(orr r0, r12, r0, lsl #8
ldr r1, [r11, #-8]
bl f
)")) << code;
}

// An 8-byte aligned struct starts at an even register; one that no longer fits is
// split between r3 and the stack.
TEST_F(Arm32Test, StructEvenAndSplit)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
struct a8 { long long x; };
struct s12 { int a, b, c; };
int f(int i, struct a8 v, struct s12 w);
int g(void) { struct a8 v = { 1 }; struct s12 w = { 2, 3, 4 }; return f(5, v, w); }
)"));
    EXPECT_NE(std::string::npos, code.find("str r12, [sp]\n")) << code; // w.b, w.c
    EXPECT_NE(std::string::npos, code.find("str r12, [sp, #4]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov r0, #5\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldr r2, [r11, #-8]
ldr r3, [r11, #-4]
)")) << code;
}

// A struct over 4 bytes is returned through the address in r0: the caller sets it, the
// callee saves it on entry and copies the value there.
TEST_F(Arm32Test, LargeStructResultThroughR0)
{
    NaiveSelection();
    std::string code = Code(CompileToArm32(R"(
struct big { int a, b, c; };
struct big make(int x) { struct big r = { x, x + 1, x + 2 }; return r; }
int use(void) { struct big b = make(5); return b.c; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(str r0, [r11, #-4]
str r1, [r11, #-8]
)")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr lr, [r11, #-4]\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov r1, #5
sub r0, r11, #24
bl make
)")) << code;
}

TEST_F(Arm32Test, RunStructs)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
struct pair { int a; char c; };
struct big { long long a, b, c; double d; };
struct pair swap(struct pair p) { struct pair r = { p.c, (char)p.a }; return r; }
struct big scale(struct big b, long long k) { b.a *= k; b.b *= k; b.c *= k; b.d *= k; return b; }
int nine(int a, int b, int c, struct pair p, struct pair q) { return a + b + c + p.a + q.c; }
struct tiny { char x, y; };
struct tiny mk(char x) { struct tiny t = { x, x + 1 }; return t; }
int main(void) {
    struct pair p = { 7, 9 };
    struct pair r = swap(p);
    struct big b = { 1, 2, 3, 0.5 };
    struct big c = scale(b, 10);
    struct pair arr[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    struct pair *pp = &arr[1];
    struct tiny t = mk('a');
    return (r.a == 9 && r.c == 7) + 2 * (c.c == 30 && c.d == 5.0 && b.a == 1)
         + 4 * (pp->c == 4) + 8 * (nine(1, 2, 3, p, r) == 6 + 7 + 7) + 16 * (t.y == 'b');
})"));
    EXPECT_EQ(31, exit_status);
}

// Structs of several sizes, an 8-byte aligned one, a large one, and one split between
// r3 and the stack, both ways with clang; results in r0 and through the address in r0.
TEST_F(Arm32Test, RunStructInteropWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    SKIP_IF_NO_ARM32_CLANG();
    const char *decls  = R"(
struct s3 { char a, b, c; };
struct s6 { short a, b, c; };
struct s12 { int a, b, c; };
struct s16 { long long a; int b; };
struct s40 { int a[10]; };
)";
    std::string ours   = std::string(decls) + R"(
int theirs(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct s6 e);
int split(int x, struct s12 b);
struct s3 theirs3(int k);
struct s12 theirs12(int k);
int call_ours(void);
int ours(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct s6 e)
{
    return a.a + a.c + b.a + b.c + (int)c.a + c.b + d.a[0] + d.a[9] + x + e.a + e.c;
}
int ours_split(int x, struct s12 b) { return x + b.a + b.b * 10 + b.c * 100; }
struct s3 ours3(int k) { struct s3 r = { k, k + 1, k + 2 }; return r; }
struct s12 ours12(int k) { struct s12 r = { k, k * 2, k * 3 }; return r; }
int main(void) {
    struct s3 a = { 1, 2, 3 };
    struct s12 b = { 4, 5, 6 };
    struct s16 c = { 7, 8 };
    struct s40 d = { { 9, 0, 0, 0, 0, 0, 0, 0, 0, 10 } };
    struct s6 e = { 11, 12, 13 };
    struct s3 r3 = theirs3(20);
    struct s12 r12 = theirs12(5);
    return (theirs(a, b, c, d, 14, e) == 1 + 3 + 4 + 6 + 7 + 8 + 9 + 10 + 14 + 11 + 13)
         + 2 * (split(1, b) == 1 + 4 + 50 + 600)
         + 4 * (r3.a == 20 && r3.c == 22) + 8 * (r12.c == 15) + 16 * call_ours();
})";
    std::string theirs = std::string(decls) + R"(
int ours(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct s6 e);
int ours_split(int x, struct s12 b);
struct s3 ours3(int k);
struct s12 ours12(int k);
int theirs(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct s6 e)
{
    return a.a + a.c + b.a + b.c + (int)c.a + c.b + d.a[0] + d.a[9] + x + e.a + e.c;
}
int split(int x, struct s12 b) { return x + b.a + b.b * 10 + b.c * 100; }
struct s3 theirs3(int k) { struct s3 r = { k, k + 1, k + 2 }; return r; }
struct s12 theirs12(int k) { struct s12 r = { k, k * 2, k * 3 }; return r; }
int call_ours(void)
{
    struct s3 a = { 1, 2, 3 };
    struct s12 b = { 4, 5, 6 };
    struct s16 c = { 7, 8 };
    struct s40 d = { { 9, 0, 0, 0, 0, 0, 0, 0, 0, 10 } };
    struct s6 e = { 11, 12, 13 };
    struct s3 r3 = ours3(20);
    struct s12 r12 = ours12(5);
    return ours(a, b, c, d, 14, e) == 1 + 3 + 4 + 6 + 7 + 8 + 9 + 10 + 14 + 11 + 13 &&
           ours_split(1, b) == 1 + 4 + 50 + 600 && r3.b == 21 && r12.b == 10;
}
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(31, exit_status);
}
