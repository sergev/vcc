//
// AVR structures: copies byte by byte from X to Z, members, and structures as
// arguments and results, checked against clang both ways.
//
#include "avr_test.h"

// A structure argument is flattened: each member an argument of its own, a char in a
// pair; a structure result comes back from r18 when over 4 bytes.
TEST_F(AvrTest, StructArgumentFlattened)
{
    NaiveSelection();
    std::string s = Body(CompileToAvr(R"(struct s5 { char a; int b; int c; };
struct s5 g(struct s5 x, int k);
int f(struct s5 *p) { return g(*p, 7).c; })"));
    EXPECT_NE(std::string::npos, s.find("ldd r24, Y+")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(ldi r18, 7
ldi r19, 0
)")) << s;
    EXPECT_NE(std::string::npos, s.find("call g\nstd Y+")) << s;
    EXPECT_NE(std::string::npos, s.find(", r18\n")) << s;
}

// clang's layout, member by member: a in r24, b in r23:r22, c in r21:r20, k r19:r18;
// a piece that does not fit above r8 goes on the stack with everything after it.
TEST_F(AvrTest, RunStructSplitWithClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    SKIP_IF_NO_AVR_CLANG();
    std::string decls = R"(void putbyte(int c);
struct s5 { char a; int b; int c; };
long g6(long long a, long long b, struct s5 s, int k);
)";
    EXPECT_EQ("ok", CompileAndRunWithClang(decls + R"(
long g6(long long a, long long b, struct s5 s, int k)
{
    return (long)a + (long)b + s.a + s.b + s.c + k;
}
)",
                                           decls + R"(
int main(void)
{
    struct s5 s = { 1, 20, 300 };
    if (g6(1000, 20000, s, 4000) != 1000 + 20000 + 1 + 20 + 300 + 4000) return 1;
    putbyte('o');
    putbyte('k');
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

TEST_F(AvrTest, RunStructs)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct point { int x, y; };
struct rec { char tag; long value; struct point p; char name[5]; };
struct rec global = { 'g', 100000, { 3, 4 }, "glob" };
struct point mid(struct point a, struct point b)
{
    struct point r = { (a.x + b.x) / 2, (a.y + b.y) / 2 };
    return r;
}
struct rec bump(struct rec r)
{
    r.value++;
    r.p.x = -r.p.x;
    return r;
}
int main(void)
{
    struct point a = { 2, 10 }, b = { 6, 20 };
    struct point m = mid(a, b);
    if (m.x != 4 || m.y != 15) return 1;
    struct rec r = bump(global);
    if (r.tag != 'g' || r.value != 100001 || r.p.x != -3 || r.p.y != 4) return 2;
    if (r.name[0] != 'g' || r.name[3] != 'b' || r.name[4] != 0) return 3;
    if (global.value != 100000) return 4;
    struct rec *pr = &r;
    pr->p = a;
    if (r.p.x != 2 || r.p.y != 10) return 5;
    struct rec copies[3];
    for (int i = 0; i < 3; i++) {
        copies[i] = r;
        copies[i].tag = 'a' + i;
    }
    if (copies[2].tag != 'c' || copies[1].value != 100001) return 6;
    union { long l; char c[4]; } u;
    u.l = 0x01020304;
    if (u.c[0] != 4 || u.c[3] != 1) return 7;
    return 0;
}
)"));
}

// Structures of 1, 3, 5, 8, 9 and 10 bytes as arguments and results, both ways
// across clang; 9 bytes and over come back through a hidden pointer.
static const char struct_decls[] = R"(
void putbyte(int c);
struct s1 { char a; };
struct s3 { char a[3]; };
struct s5 { char a; int b; int c; };
struct s8 { long a; long b; };
struct s9 { char a[9]; };
struct s10 { int a[5]; };
struct s1 f1(struct s1 x, int k);
struct s3 f3(struct s3 x, int k);
struct s5 f5(struct s5 x, int k);
struct s8 f8(struct s8 x, int k);
struct s9 f9(struct s9 x, int k);
struct s10 f10(long pad, struct s10 x, int k);
)";

static const char struct_callee[] = R"(
struct s1 f1(struct s1 x, int k) { x.a += k; return x; }
struct s3 f3(struct s3 x, int k) { x.a[0] += k; x.a[2] -= k; return x; }
struct s5 f5(struct s5 x, int k) { x.a += k; x.b *= k; x.c -= k; return x; }
struct s8 f8(struct s8 x, int k) { x.a += k; x.b -= k; return x; }
struct s9 f9(struct s9 x, int k) { for (int i = 0; i < 9; i++) x.a[i] += k; return x; }
struct s10 f10(long pad, struct s10 x, int k)
{
    for (int i = 0; i < 5; i++) x.a[i] += k + (int)pad;
    return x;
}
)";

static const char struct_caller[] = R"(
int main(void)
{
    struct s1 a = { 10 };
    struct s3 b = { { 1, 2, 3 } };
    struct s5 c = { 1, 300, 5 };
    struct s8 d = { 100000, -5 };
    struct s9 e = { { 1, 2, 3, 4, 5, 6, 7, 8, 9 } };
    struct s10 f = { { 1, 2, 3, 4, 5 } };
    a = f1(a, 2);
    if (a.a != 12) return 1;
    b = f3(b, 1);
    if (b.a[0] != 2 || b.a[1] != 2 || b.a[2] != 2) return 2;
    c = f5(c, 3);
    if (c.a != 4 || c.b != 900 || c.c != 2) return 3;
    d = f8(d, 7);
    if (d.a != 100007 || d.b != -12) return 4;
    e = f9(e, 10);
    if (e.a[0] != 11 || e.a[8] != 19) return 5;
    f = f10(1000, f, 1);
    if (f.a[0] != 1002 || f.a[4] != 1006) return 6;
    putbyte('o');
    putbyte('k');
    return 0;
}
)";

TEST_F(AvrTest, RunStructsToClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    SKIP_IF_NO_AVR_CLANG();
    EXPECT_EQ("ok", CompileAndRunWithClang(std::string(struct_decls) + struct_caller,
                                           std::string(struct_decls) + struct_callee));
    EXPECT_EQ(0, exit_status);
}

TEST_F(AvrTest, RunStructsFromClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    SKIP_IF_NO_AVR_CLANG();
    EXPECT_EQ("ok", CompileAndRunWithClang(std::string(struct_decls) + struct_callee,
                                           std::string(struct_decls) + struct_caller));
    EXPECT_EQ(0, exit_status);
}
