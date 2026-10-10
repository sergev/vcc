//
// AArch64 structs: member access, copies, and passing and returning them by value
// (AAPCS64 composites other than homogeneous float aggregates).
//
#include "aarch64_test.h"

// A 12-byte struct goes in two X registers: a whole doubleword, then 4 bytes.
TEST_F(Aarch64Test, SmallStructInRegisters)
{
    aarch64_frame_pointer = true;  // slots at x29 offsets
    aarch64_peephole      = false; // the ABI, not its clean-up
    std::string code      = Code(CompileToAarch64(R"(
struct s { int a, b, c; };
int f(struct s v);
int g(void) { struct s v = { 1, 2, 3 }; return f(v); }
)"));
    EXPECT_NE(std::string::npos, code.find("ldr x0, [x29, #-")) << code;
    EXPECT_NE(std::string::npos, code.find("ldrb w1, [x29, #")) << code;
    EXPECT_NE(std::string::npos, code.find("orr x1, x11, x1, lsl #8\n")) << code;
}

// A struct over 16 bytes is returned through the address in x8: the caller sets it,
// the callee saves it on entry and copies the value there.
TEST_F(Aarch64Test, LargeStructResultThroughX8)
{
    aarch64_frame_pointer = true;  // slots at x29 offsets
    aarch64_peephole      = false; // the ABI, not its clean-up
    std::string code      = Code(CompileToAarch64(R"(
struct big { long a, b, c; };
struct big make(long x) { struct big r = { x, x + 1, x + 2 }; return r; }
long use(void) { struct big b = make(5); return b.c; }
)"));
    EXPECT_NE(std::string::npos, code.find("str x8, [x29, #-8]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr x13, [x29, #-8]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sub x8, x29, #")) << code;
}

TEST_F(Aarch64Test, RunStructs)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
struct pair { int a; char c; };
struct big { long a, b, c; double d; };
struct pair swap(struct pair p) { struct pair r = { p.c, (char)p.a }; return r; }
struct big scale(struct big b, long k) { b.a *= k; b.b *= k; b.c *= k; b.d *= k; return b; }
long nine(long a, long b, long c, long d, long e, long f, long g, struct pair p, struct pair q)
{
    return a + b + c + d + e + f + g + p.a + q.c;
}
int main(void) {
    struct pair p = { 7, 9 };
    struct pair r = swap(p);
    struct big b = { 1, 2, 3, 0.5 };
    struct big c = scale(b, 10);
    struct pair arr[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    struct pair *pp = &arr[1];
    return (r.a == 9 && r.c == 7) + 2 * (c.c == 30 && c.d == 5.0 && b.a == 1)
         + 4 * (pp->c == 4) + 8 * (nine(1, 2, 3, 4, 5, 6, 7, p, r) == 7 + 28 + 7);
})"));
    EXPECT_EQ(15, exit_status);
}

// Structs of every size up to 16 bytes, an odd one, a large one and a 16-byte-aligned
// one, both ways with clang; the last argument no longer fits the registers and goes
// whole on the stack.
TEST_F(Aarch64Test, RunStructInteropWithClang)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    SKIP_IF_NO_AARCH64_CLANG();
    const char *decls  = R"(
struct s3 { char a, b, c; };
struct s12 { int a, b, c; };
struct s16 { long a, b; };
struct s40 { long a[5]; };
struct al { _Alignas(16) long a; long b; };
)";
    std::string ours   = std::string(decls) + R"(
long theirs(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct al e,
            struct s16 f);
struct s12 theirs12(int k);
struct s40 theirs40(int k);
int call_ours(void);
long ours(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct al e,
          struct s16 f)
{
    return a.a + a.c + b.c + c.b + d.a[4] + x + e.b + f.a * 1000;
}
struct s12 ours12(int k) { struct s12 r = { k, k + 1, k + 2 }; return r; }
struct s40 ours40(int k) { struct s40 r = { { k, 0, 0, 0, k * 2 } }; return r; }
int main(void) {
    struct s3 a = { 1, 2, 3 };
    struct s12 b = { 4, 5, 6 };
    struct s16 c = { 7, 8 };
    struct s40 d = { { 9, 10, 11, 12, 13 } };
    struct al e = { 14, 15 };
    struct s16 f = { 16, 17 };
    struct s12 r = theirs12(20);
    struct s40 q = theirs40(30);
    return (theirs(a, b, c, d, 100, e, f) == 1 + 3 + 6 + 8 + 13 + 100 + 15 + 16000)
         + 2 * (r.c == 22) + 4 * (q.a[4] == 60) + 8 * call_ours();
})";
    std::string theirs = std::string(decls) + R"(
long ours(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct al e,
          struct s16 f);
struct s12 ours12(int k);
struct s40 ours40(int k);
long theirs(struct s3 a, struct s12 b, struct s16 c, struct s40 d, int x, struct al e,
            struct s16 f)
{
    return a.a + a.c + b.c + c.b + d.a[4] + x + e.b + f.a * 1000;
}
struct s12 theirs12(int k) { struct s12 r = { k, k + 1, k + 2 }; return r; }
struct s40 theirs40(int k) { struct s40 r = { { k, 0, 0, 0, k * 2 } }; return r; }
int call_ours(void)
{
    struct s3 a = { 1, 2, 3 };
    struct s12 b = { 4, 5, 6 };
    struct s16 c = { 7, 8 };
    struct s40 d = { { 9, 10, 11, 12, 13 } };
    struct al e = { 14, 15 };
    struct s16 f = { 16, 17 };
    return ours(a, b, c, d, 100, e, f) == 1 + 3 + 6 + 8 + 13 + 100 + 15 + 16000 &&
           ours12(20).c == 22 && ours40(30).a[4] == 60;
})";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(15, exit_status);
}
