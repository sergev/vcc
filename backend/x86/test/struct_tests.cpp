//
// x86-64 structs: member access at offsets, whole copies through r11 (a loop past 64
// bytes), and the System V classes: up to 16 bytes in registers by eightbyte, all or
// nothing, the rest on the stack; results in rax/rdx and xmm0/xmm1, st(0), or through
// the address in rdi.
//
#include "x86_test.h"

#define EXPECT_HAS(name, expected, src)                                    \
    TEST_F(X86Test, name)                                                  \
    {                                                                      \
        NaiveSelection();                                                  \
        std::string code = Code(CompileToX86(src));                        \
        EXPECT_NE(std::string::npos, code.find(expected)) << code;         \
    }

TEST(TacAbi, Sysv64Class)
{
    Tac_Type c = { .kind = TAC_TYPE_SCHAR }, i = { .kind = TAC_TYPE_INT },
             l = { .kind = TAC_TYPE_LONG }, f = { .kind = TAC_TYPE_FLOAT },
             d = { .kind = TAC_TYPE_DOUBLE }, ld = { .kind = TAC_TYPE_LONG_DOUBLE };
    EXPECT_EQ(TAC_SYSV64_INTEGER, tac_sysv64_class(&i));
    EXPECT_EQ(TAC_SYSV64_SSE, tac_sysv64_class(&d));
    EXPECT_EQ(TAC_SYSV64_X87, tac_sysv64_class(&ld));

    // { double; long } is SSE then INTEGER.
    Tac_Member m2 = { .name = const_cast<char *>("b"), .offset = 8, .type = &l };
    Tac_Member m1 = { .next = &m2, .name = const_cast<char *>("a"), .offset = 0, .type = &d };
    Tac_Type s    = { .kind = TAC_TYPE_STRUCTURE };
    s.u.structure.members = &m1;
    s.u.structure.size    = 16;
    EXPECT_EQ(TAC_SYSV64_SSE | TAC_SYSV64_INTEGER << 2, tac_sysv64_class(&s));

    // { float; float; int }: two floats share an SSE eightbyte, the int its own.
    Tac_Member n3 = { .name = const_cast<char *>("c"), .offset = 8, .type = &i };
    Tac_Member n2 = { .next = &n3, .name = const_cast<char *>("b"), .offset = 4, .type = &f };
    Tac_Member n1 = { .next = &n2, .name = const_cast<char *>("a"), .offset = 0, .type = &f };
    s.u.structure.members = &n1;
    s.u.structure.size    = 12;
    EXPECT_EQ(TAC_SYSV64_SSE | TAC_SYSV64_INTEGER << 2, tac_sysv64_class(&s));
    // { float; char; int }: the float merges with the char into INTEGER.
    n2.type = &c;
    EXPECT_EQ(TAC_SYSV64_INTEGER | TAC_SYSV64_INTEGER << 2, tac_sysv64_class(&s));

    // { char[3] } is one INTEGER eightbyte; seventeen bytes are MEMORY.
    Tac_Type a = { .kind = TAC_TYPE_ARRAY };
    a.u.array.elem_type = &c;
    a.u.array.size      = 3;
    EXPECT_EQ(TAC_SYSV64_INTEGER, tac_sysv64_class(&a));
    a.u.array.size = 17;
    EXPECT_EQ(TAC_SYSV64_MEMORY, tac_sysv64_class(&a));

    // { long double } is X87; { long double; int } and a union of long double and
    // double are MEMORY.
    Tac_Member x1 = { .name = const_cast<char *>("v"), .offset = 0, .type = &ld };
    s.u.structure.members = &x1;
    s.u.structure.size    = 16;
    EXPECT_EQ(TAC_SYSV64_X87, tac_sysv64_class(&s));
    Tac_Member x2 = { .name = const_cast<char *>("k"), .offset = 16, .type = &i };
    x1.next              = &x2;
    s.u.structure.size   = 32;
    EXPECT_EQ(TAC_SYSV64_MEMORY, tac_sysv64_class(&s));
    Tac_Member u2 = { .name = const_cast<char *>("d"), .offset = 0, .type = &d };
    Tac_Member u1 = { .next = &u2, .name = const_cast<char *>("v"), .offset = 0, .type = &ld };
    s.u.structure.members  = &u1;
    s.u.structure.size     = 16;
    s.u.structure.is_union = true;
    EXPECT_EQ(TAC_SYSV64_MEMORY, tac_sysv64_class(&s));
}

// Eightbytes in registers of either class, mixed included: SSE then INTEGER, an odd
// size put together from pieces without reading past the end, two floats in one xmm.
EXPECT_HAS(RegisterArguments, R"(movsd -16(%rbp), %xmm0
movq -8(%rbp), %rdi
movzbl -17(%rbp), %esi
shlq $16, %rsi
movzwl -19(%rbp), %r10d
orq %r10, %rsi
movsd -32(%rbp), %xmm1
movss -24(%rbp), %xmm2
call g
)",
           R"(struct m { double d; long l; };
struct c3 { char c[3]; };
struct f3 { float a, b, c; };
long g(struct m x, struct c3 y, struct f3 z);
long f(void) { struct m x = { 1, 2 }; struct c3 y = { "ab" }; struct f3 z = { 1, 2, 3 }; return g(x, y, z); }
)")

// The callee stores the registers into its slots, in pieces where the size is odd;
// the result goes back in xmm0/xmm1.
EXPECT_HAS(RegisterParameters, R"(movw %di, -3(%rbp)
shrq $16, %rdi
movb %dil, -1(%rbp)
movsd %xmm0, -24(%rbp)
movq %rsi, -16(%rbp)
)",
           R"(struct m { double d; long l; };
struct c3 { char c[3]; };
struct f3 { float a, b, c; };
struct f3 r(struct c3 y, struct m x) { struct f3 z = { x.d, y.c[1], 3 }; return z; }
)")
EXPECT_HAS(RegisterResult, "movsd -36(%rbp), %xmm0\nmovss -28(%rbp), %xmm1\nleave\nret\n",
           R"(struct m { double d; long l; };
struct c3 { char c[3]; };
struct f3 { float a, b, c; };
struct f3 r2(struct c3 y, struct m x) { struct f3 z = { x.d, y.c[1], 3 }; return z; }
)")

// A mixed result comes back in rax and xmm0.
EXPECT_HAS(MixedResult, "call h\nmovq %rax, -32(%rbp)\nmovsd %xmm0, -24(%rbp)\n",
           "struct m { int i; double d; }; struct m h(void);"
           "double f(void) { struct m x = h(); return x.d + x.i; }")

// All or nothing: two eightbytes do not fit in r9 alone, so the struct goes on the
// stack and the next argument still takes r9.
EXPECT_HAS(AllOrNothing, R"(movq -16(%rbp), %r11
movq %r11, (%rsp)
movq -8(%rbp), %r11
movq %r11, 8(%rsp)
movl $1, %edi
movl $2, %esi
movl $3, %edx
movl $4, %ecx
movl $5, %r8d
movl $6, %r9d
call k
)",
           "struct p { long a, b; }; int k(long a, long b, long c, long d, long e, struct p x, long z);"
           "int f(void) { struct p x = { 1, 2 }; return k(1, 2, 3, 4, 5, x, 6); }")

// A MEMORY struct is copied onto the stack, the widest pieces the alignment allows,
// padding included.
EXPECT_HAS(CopyPieces, R"(movq -24(%rbp), %r11
movq %r11, (%rsp)
movq -16(%rbp), %r11
movq %r11, 8(%rsp)
movq -8(%rbp), %r11
movq %r11, 16(%rsp)
movl $1, %edi
call g
)",
           "struct s { long a, b; int c; }; int g(int n, struct s x);"
           "int f(void) { struct s x = { 1, 2, 3 }; return g(1, x); }")

// Past 64 bytes a loop of 16 bytes through xmm15, counting in r11 (no allocated
// register changes), then the rest.
EXPECT_HAS(CopyLoop, "leaq (%rsp), %rax\nleaq -84(%rbp), %r10\nmovl $4, %r11d\n"
                     "movups (%r10), %xmm15\nmovups %xmm15, (%rax)\naddq $16, %r10\n"
                     "addq $16, %rax\nsubl $1, %r11d\njne .Lx0\nmovl (%r10), %r11d\n"
                     "movl %r11d, (%rax)\nmovl 4(%r10), %r11d\nmovl %r11d, 4(%rax)\n"
                     "movl 8(%r10), %r11d\nmovl %r11d, 8(%rax)\ncall g\n",
           "struct s { int a[19]; }; int g(struct s x); int f(struct s *p) { return g(*p); }")

// A member is a load or store at its offset in the slot.
EXPECT_HAS(MemberAccess, "movl $5, -8(%rbp)\nmovl -8(%rbp), %eax\n",
           "struct s { long a; int b; }; int f(void) { struct s x; x.b = 5; return x.b; }")

// A MEMORY result is written through the address that came in rdi, which goes back
// in rax; the parameters start at rsi.
EXPECT_CODE(StructResultCallee, R"(pushq %rbp
movq %rsp, %rbp
subq $48, %rsp
movq %rdi, -8(%rbp)
movq %rsi, -16(%rbp)
movq -16(%rbp), %rax
movq %rax, -40(%rbp)
movq $2, -32(%rbp)
movq $3, -24(%rbp)
movq -8(%rbp), %r10
movq -40(%rbp), %r11
movq %r11, (%r10)
movq -32(%rbp), %r11
movq %r11, 8(%r10)
movq -24(%rbp), %r11
movq %r11, 16(%r10)
movq -8(%rbp), %rax
leave
ret
)",
            "struct q { long a, b, c; }; struct q f(long a) { struct q x = { a, 2, 3 }; return x; }")
// The caller passes the destination's address.
EXPECT_HAS(StructResultCaller, "movl $7, %esi\nleaq -48(%rbp), %rdi\ncall g\n",
           "struct q { long a, b, c; }; struct q g(long a);"
           "long f(void) { struct q x = g(7); return x.b; }")

// A struct of a long double goes on the stack and comes back in st(0).
EXPECT_CODE(X87StructResult, R"(pushq %rbp
movq %rsp, %rbp
fldt 16(%rbp)
leave
ret
)",
            "struct L { long double v; }; struct L k(struct L a) { return a; }")
EXPECT_HAS(X87StructCaller, "call k\nfstpt -32(%rbp)\n",
           "struct L { long double v; }; struct L k(struct L a);"
           "long double f(void) { struct L x = { 1 }; return k(x).v; }")

TEST_F(X86Test, RunStructs)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
struct small { char c; short s; };
struct big { long v[10]; double d; };
struct big make(long base) {
    struct big b;
    for (int i = 0; i < 10; i++)
        b.v[i] = base + i;
    b.d = 0.5;
    return b;
}
long sum(struct big b, struct small s, int k) {
    long t = 0;
    for (int i = 0; i < 10; i++)
        t += b.v[i];
    return t + s.c + s.s + k + (long)(b.d * 2);
}
int main(void) {
    struct big b = make(100);
    struct big copy = b;
    struct small s = { -3, 300 };
    struct big *p = &copy;
    p->v[9] = 1000;
    if (b.v[9] != 109 || copy.v[9] != 1000 || copy.v[0] != 100)
        return 1;
    if (sum(b, s, 5) != 1045 + 297 + 5 + 1)
        return 2;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}

// Structs of every class with clang both ways: arguments, results and all or nothing.
TEST_F(X86Test, RunStructsWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    const char *decls = R"(
struct m { double d; long l; };
struct n { long l; double d; };
struct c3 { char c[3]; };
struct f3 { float a, b, c; };
struct p { long a, b; };
struct q { long a, b, c; };
struct L { long double v; };
long theirs(struct m a, struct c3 b, struct f3 c, long d, long e, long f, struct p g, long h);
struct n theirs_n(struct m a);
struct q theirs_q(struct p a, struct L b);
struct L theirs_l(struct L a, struct c3 b);
struct f3 theirs_f3(float x);
long ours(struct m a, struct c3 b, struct f3 c, long d, long e, long f, struct p g, long h);
struct n ours_n(struct m a);
struct q ours_q(struct p a, struct L b);
struct L ours_l(struct L a, struct c3 b);
struct f3 ours_f3(float x);
)";
    CompileAndRunWithClang(std::string(decls) + R"(
long ours(struct m a, struct c3 b, struct f3 c, long d, long e, long f, struct p g, long h) {
    return (long)a.d + a.l * 10 + b.c[0] + b.c[2] * 100 + (long)(c.a + c.b + c.c)
           + d + e + f + g.a * 1000 + g.b * 10000 + h * 100000;
}
struct n ours_n(struct m a) { struct n r = { a.l + 1, a.d * 2 }; return r; }
struct q ours_q(struct p a, struct L b) { struct q r = { a.a, a.b, (long)b.v }; return r; }
struct L ours_l(struct L a, struct c3 b) { struct L r = { a.v * b.c[1] }; return r; }
struct f3 ours_f3(float x) { struct f3 r = { x, x * 2, x * 3 }; return r; }
int main(void) {
    struct m a = { 1.5, 2 };
    struct c3 b = { { 3, 4, -5 } };
    struct f3 c = { 0.5, 1.5, 2 };
    struct p g = { 6, 7 };
    if (theirs(a, b, c, 1, 2, 3, g, 8) != 1 + 20 + 3 - 500 + 4 + 6 + 6000 + 70000 + 800000)
        return 1;
    struct n r = theirs_n(a);
    if (r.l != 3 || r.d != 3.0)
        return 2;
    struct L l = { 2.5L };
    struct q s = theirs_q(g, l);
    if (s.a != 6 || s.b != 7 || s.c != 2)
        return 3;
    if (theirs_l(l, b).v != 10.0L)
        return 4;
    struct f3 t = theirs_f3(1.5f);
    if (t.a != 1.5f || t.b != 3.0f || t.c != 4.5f)
        return 5;
    return 42;
})",
                           std::string(decls) + R"(
long theirs(struct m a, struct c3 b, struct f3 c, long d, long e, long f, struct p g, long h) {
    return ours(a, b, c, d, e, f, g, h);
}
struct n theirs_n(struct m a) { return ours_n(a); }
struct q theirs_q(struct p a, struct L b) { return ours_q(a, b); }
struct L theirs_l(struct L a, struct c3 b) { return ours_l(a, b); }
struct f3 theirs_f3(float x) { return ours_f3(x); }
)");
    EXPECT_EQ(42, exit_status);
}
