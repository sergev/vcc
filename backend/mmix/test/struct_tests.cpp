//
// MMIX structures: copies by the alignment's width, a structure of 8 bytes or less in a
// register right-justified, a larger one by reference (the callee copies it), and every
// structure result through the address in $251; with GCC both ways.
//
#include "mmix_test.h"

// A 3-byte structure goes right-justified, its bytes assembled; a 4-aligned 8-byte one
// in two tetras.
TEST_F(MmixTest, SmallStructArgument)
{
    std::string code = Code(CompileToMmix(R"(
        struct c3 { char a, b, c; };
        struct i2 { int a, b; };
        long g3(struct c3); long g8(struct i2);
        long f1(struct c3 *p) { return g3(*p); }
        long f2(struct i2 *p) { return g8(*p); }
    )"));
    EXPECT_NE(std::string::npos, code.find("ldbu $2,$254,8\n"
                                           "ldbu $1,$254,9\n"
                                           "slu $2,$2,8\n"
                                           "or $2,$2,$1\n"
                                           "ldbu $1,$254,10\n"
                                           "slu $2,$2,8\n"
                                           "or $2,$2,$1\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("ldtu $2,$254,8\nldtu $1,$254,12\nslu $2,$2,32\n"))
        << code;
}

// The callee stores a small structure parameter back, its last piece first.
TEST_F(MmixTest, SmallStructParameter)
{
    std::string code =
        Code(CompileToMmix("struct c3 { char a, b, c; }; char f(struct c3 s) { return s.b; }"));
    EXPECT_NE(std::string::npos, code.find("stbu $0,$254,2\nsru $0,$0,8\n"
                                           "stbu $0,$254,1\nsru $0,$0,8\n"
                                           "stbu $0,$254,0\n"))
        << code;
}

// A structure result goes through $251: the caller points it at the destination, the
// callee saves it on entry and copies the result there, and pops nothing.
TEST_F(MmixTest, StructResult)
{
    std::string code = Code(CompileToMmix(R"(
        struct p { long x, y; };
        struct p make(long x) { struct p r = { x, x + 1 }; return r; }
        long use(void) { struct p q = make(5); return q.y; }
    )"));
    EXPECT_NE(std::string::npos, code.find("sto $251,$254,")) << code;
    EXPECT_NE(std::string::npos, code.find("pop 0,0\n")) << code;
    EXPECT_NE(std::string::npos, code.find("addu $251,$254,")) << code;
}

// Run: structures of every size as arguments and results, among scalars, ours both
// ways; a callee writing its large parameter leaves the caller's object as it was.
TEST_F(MmixTest, RunStructs)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        struct s1 { char a; };
        struct s3 { char a, b, c; };
        struct s5 { char a[5]; };
        struct s8 { int a, b; };
        struct s9 { char a[9]; };
        struct s24 { long a, b, c; };
        union u { int i; char c[4]; };
        long f(int k, struct s1 a, struct s3 b, long m, struct s5 c, struct s8 d, struct s9 e,
               struct s24 g, union u h)
        {
            long r = k + a.a + b.a + b.b + b.c + m + c.a[0] + c.a[4] + d.a + d.b;
            r += e.a[0] + e.a[8] + g.a + g.b + g.c + h.c[0];
            e.a[0] = 99;
            g.a = 99;
            return r;
        }
        struct s3 make3(char x) { struct s3 s = { x, x + 1, x + 2 }; return s; }
        struct s24 make24(long x) { struct s24 s = { x, 2 * x, 3 * x }; return s; }
        struct s1 make1(char x) { struct s1 s = { x }; return s; }
        int main(void)
        {
            struct s1 a = { 1 };
            struct s3 b = { 2, 3, 4 };
            struct s5 c = { { 5, 0, 0, 0, 6 } };
            struct s8 d = { 7, 8 };
            struct s9 e = { { 9, 0, 0, 0, 0, 0, 0, 0, 10 } };
            struct s24 g = { 11, 12, 13 };
            union u h;
            h.i = 0x0e000000;
            if (f(100, a, b, 1000, c, d, e, g, h) != 1205) return 1;
            if (e.a[0] != 9 || g.a != 11) return 2;
            struct s3 m = make3(20);
            if (m.a != 20 || m.b != 21 || m.c != 22) return 3;
            struct s24 n = make24(7);
            if (n.a != 7 || n.b != 14 || n.c != 21) return 4;
            if (make1(33).a != 33) return 5;
            make24(1);
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

static const char struct_types[] = R"(
    struct s1 { char a; };
    struct s2 { short a; };
    struct s3 { char a, b, c; };
    struct s4 { int a; };
    struct s5 { char a[5]; };
    struct s8 { int a, b; };
    struct s9 { char a[9]; };
    struct s16 { long a, b; };
    struct s24 { long a, b, c; };
    union u { int i; char c[4]; };
)";

// GCC's side: a function taking every size among scalars, results of every size, and a
// caller of ours.
static const char gcc_side[] = R"(
    long gf(int k, struct s1 a, struct s2 b, struct s3 c, struct s4 d, long m, struct s5 e,
            struct s8 f, struct s9 g, struct s16 h, struct s24 i, union u j)
    {
        long r = k + a.a + b.a + c.a + c.c + d.a + m + e.a[0] + e.a[4] + f.a + f.b;
        r += g.a[0] + g.a[8] + h.a + h.b + i.a + i.c + j.c[0];
        g.a[0] = 99;
        i.a = 99;
        return r;
    }
    struct s1 gm1(char x) { struct s1 s = { x }; return s; }
    struct s3 gm3(char x) { struct s3 s = { x, x + 1, x + 2 }; return s; }
    struct s8 gm8(int x) { struct s8 s = { x, x + 1 }; return s; }
    struct s24 gm24(long x) { struct s24 s = { x, 2 * x, 3 * x }; return s; }
    long of(int k, struct s1 a, struct s2 b, struct s3 c, struct s4 d, long m, struct s5 e,
            struct s8 f, struct s9 g, struct s16 h, struct s24 i, union u j);
    struct s3 om3(char x);
    struct s24 om24(long x);
    long gcall(void)
    {
        struct s1 a = { 1 }; struct s2 b = { 2 }; struct s3 c = { 3, 0, 4 }; struct s4 d = { 5 };
        struct s5 e = { { 6, 0, 0, 0, 7 } }; struct s8 f = { 8, 9 };
        struct s9 g = { { 10, 0, 0, 0, 0, 0, 0, 0, 11 } }; struct s16 h = { 12, 13 };
        struct s24 i = { 14, 15, 16 }; union u j; j.i = 0x11000000;
        long r = of(100, a, b, c, d, 1000, e, f, g, h, i, j);
        if (g.a[0] != 10 || i.a != 14) return -1;
        struct s3 m = om3(30);
        struct s24 n = om24(5);
        return r + m.a + m.c + n.c;
    }
)";

// Run: structures of every size between our code and GCC's, both ways: right-justified
// in a register up to 8 bytes, by reference beyond (the callee copies), and every result
// through $251.
TEST_F(MmixTest, RunStructsWithGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc  = std::string(struct_types) + gcc_side;
    std::string ours = CompileToMmix((std::string(struct_types) + R"(
        long gf(int k, struct s1 a, struct s2 b, struct s3 c, struct s4 d, long m, struct s5 e,
                struct s8 f, struct s9 g, struct s16 h, struct s24 i, union u j);
        struct s1 gm1(char x); struct s3 gm3(char x); struct s8 gm8(int x); struct s24 gm24(long x);
        long gcall(void);
        long of(int k, struct s1 a, struct s2 b, struct s3 c, struct s4 d, long m, struct s5 e,
                struct s8 f, struct s9 g, struct s16 h, struct s24 i, union u j)
        {
            long r = k + a.a + b.a + c.a + c.c + d.a + m + e.a[0] + e.a[4] + f.a + f.b;
            r += g.a[0] + g.a[8] + h.a + h.b + i.a + i.c + j.c[0];
            g.a[0] = 99;
            i.a = 99;
            return r;
        }
        struct s3 om3(char x) { struct s3 s = { x, x + 1, x + 2 }; return s; }
        struct s24 om24(long x) { struct s24 s = { x, 2 * x, 3 * x }; return s; }
        int main(void)
        {
            struct s1 a = { 1 }; struct s2 b = { 2 }; struct s3 c = { 3, 0, 4 }; struct s4 d = { 5 };
            struct s5 e = { { 6, 0, 0, 0, 7 } }; struct s8 f = { 8, 9 };
            struct s9 g = { { 10, 0, 0, 0, 0, 0, 0, 0, 11 } }; struct s16 h = { 12, 13 };
            struct s24 i = { 14, 15, 16 }; union u j; j.i = 0x11000000;
            if (gf(100, a, b, c, d, 1000, e, f, g, h, i, j) != 1238) return 1;
            if (g.a[0] != 10 || i.a != 14) return 2;
            if (gm1(40).a != 40) return 3;
            struct s3 m = gm3(50);
            if (m.a != 50 || m.b != 51 || m.c != 52) return 4;
            struct s8 n = gm8(60);
            if (n.a != 60 || n.b != 61) return 5;
            struct s24 o = gm24(70);
            if (o.a != 70 || o.b != 140 || o.c != 210) return 6;
            if (gcall() != 1238 + 30 + 32 + 15) return 7;
            return 0;
        }
    )")
                                         .c_str());
    EXPECT_EQ("", Run(ours, "crt0.o", &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}
