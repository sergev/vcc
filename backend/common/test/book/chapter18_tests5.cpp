//
// Bit-fields, which the book leaves out of chapter 18.  Not the book's programs: they
// follow its form (main returns 0, or the number of the check that failed) and fit
// every target -- a 16-bit int, so wider fields are long, and BESM-6's 41-bit long long.
//
#include "book_test.h"

// Reads and writes: each field keeps its own bits, a signed field reads back sign-extended,
// an unsigned one zero-extended, and a value too wide is truncated to the field.
TEST_F(BookTest, Chapter18_BitfieldReadWrite)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct s {
    unsigned a : 3;
    int b : 5;
    char c;
    int d : 12;
    unsigned long e : 20;
    long f : 31;
    _Bool g : 1;
    unsigned h : 1;
};

int check(struct s *p, unsigned a, int b, char c, int d, unsigned long e, long f, int g, unsigned h)
{
    return p->a == a && p->b == b && p->c == c && p->d == d && p->e == e && p->f == f &&
           p->g == g && p->h == h;
}

int main(void) {
    struct s x;
    struct s *p = &x;
    x.a = 5;
    x.b = -7;
    x.c = 'q';
    x.d = -1000;
    x.e = 0xabcdeul;
    x.f = -123456789l;
    x.g = 1;
    x.h = 1;
    if (!check(&x, 5, -7, 'q', -1000, 0xabcdeul, -123456789l, 1, 1))
        return 1;
    // Change each field in turn; the others must not move.
    p->a = 2;
    if (!check(&x, 2, -7, 'q', -1000, 0xabcdeul, -123456789l, 1, 1))
        return 2;
    p->b = 15;
    if (!check(&x, 2, 15, 'q', -1000, 0xabcdeul, -123456789l, 1, 1))
        return 3;
    p->d = 2047;
    if (!check(&x, 2, 15, 'q', 2047, 0xabcdeul, -123456789l, 1, 1))
        return 4;
    p->e = 0;
    if (!check(&x, 2, 15, 'q', 2047, 0, -123456789l, 1, 1))
        return 5;
    p->f = 1073741823l;
    if (!check(&x, 2, 15, 'q', 2047, 0, 1073741823l, 1, 1))
        return 6;
    x.g = 0;
    x.h = 0;
    if (!check(&x, 2, 15, 'q', 2047, 0, 1073741823l, 0, 0))
        return 7;
    // Truncation to the field.
    x.a = 13;      // 0b1101
    x.b = 33;      // 0b100001
    x.d = 4096 + 5;
    x.e = 0x123456ul;
    if (x.a != 5 || x.b != 1 || x.d != 5 || x.e != 0x23456ul)
        return 8;
    x.b = 16;      // the sign bit alone
    x.d = 2048;
    if (x.b != -16 || x.d != -2048)
        return 9;
    // A conversion to _Bool is a zero test, not a truncation.
    x.g = 2;
    if (x.g != 1)
        return 10;
    if (x.c != 'q')
        return 11;
    return 0;
})"));
}

// Compound assignment, ++ and --, the value of an assignment, and the promotions: a
// bit-field narrower than int promotes to int, even an unsigned one.
TEST_F(BookTest, Chapter18_BitfieldArithmetic)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct s {
    int i : 6;
    unsigned u : 3;
    long l : 20;
    unsigned w : 7;
};

int main(void) {
    struct s x = { 0, 0, 0, 0 };
    struct s *p = &x;
    x.i = 10;
    x.i *= -3;              // -30
    if (x.i != -30)
        return 1;
    x.i -= 5;               // -35 wraps to 29
    if (x.i != 29)
        return 2;
    p->u = 6;
    p->u /= -2;             // int division: -3, stored as 5
    if (p->u != 5)
        return 3;
    if (x.u - 7 >= 0)       // promoted to int: -2
        return 4;
    p->l = 1000;
    p->l <<= 10;            // 1024000 wraps to -24576 in 20 bits
    if (p->l != -24576l)
        return 5;
    p->l >>= 4;
    if (p->l != -1536)
        return 6;
    x.w = 126;
    if (x.w++ != 126 || x.w != 127)
        return 7;
    if (++x.w != 0 || x.w != 0)   // wraps
        return 8;
    if (x.w-- != 0 || x.w != 127)
        return 9;
    p->i = 31;
    if (++p->i != -32)
        return 10;
    if (p->i-- != -32 || p->i != 31)
        return 11;
    // The value of an assignment is the value the field holds afterwards.
    if ((x.u = 9) != 1)
        return 12;
    if ((p->i = 40) != -24)
        return 13;
    if ((x.w |= 0x80) != 127)
        return 14;
    int sum = x.i + x.u + x.w;   // -24 + 1 + 127
    if (sum != 104)
        return 15;
    return 0;
})"));
}

// Static and automatic initializers: positional, designated, with unnamed bit-fields
// (skipped by initialization) and :0, in arrays, nested structs and unions.  A static
// instance and one built by stores must hold the same fields.
TEST_F(BookTest, Chapter18_BitfieldInitializers)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct s {
    char x;
    int : 4;
    int y : 6;
    int : 0;
    unsigned long z : 30;
    unsigned w : 9;
};

struct outer {
    int n;
    struct s in;
    unsigned flag : 1;
};

union u {
    int i : 7;
    unsigned char c;
};

struct s g1 = { 'k', -20, 123456789ul, 300 };
struct s g2 = { .w = 511, .y = 31 };
struct s garr[3] = { { 'a', 1, 2, 3 }, [2] = { .z = 99 } };
struct outer go = { 7, { 'b', -1, 5, 6 }, 1 };
union u gu = { -5 };
static struct s gs;

int same(struct s *a, struct s *b) {
    return a->x == b->x && a->y == b->y && a->z == b->z && a->w == b->w;
}

int main(void) {
    struct s l1 = { 'k', -20, 123456789ul, 300 };
    struct s l2 = { .w = 511, .y = 31 };
    struct s larr[3] = { { 'a', 1, 2, 3 }, [2] = { .z = 99 } };
    struct outer lo = { 7, { 'b', -1, 5, 6 }, 1 };
    union u lu = { -5 };
    struct s byhand;
    byhand.x = 'k';
    byhand.y = -20;
    byhand.z = 123456789ul;
    byhand.w = 300;
    if (!same(&g1, &byhand) || !same(&l1, &byhand))
        return 1;
    if (g1.y != -20 || g1.z != 123456789ul || g1.w != 300 || g1.x != 'k')
        return 2;
    if (g2.x != 0 || g2.y != 31 || g2.z != 0 || g2.w != 511)
        return 3;
    if (!same(&g2, &l2))
        return 4;
    for (int i = 0; i < 3; i++)
        if (!same(&garr[i], &larr[i]))
            return 5;
    if (garr[0].w != 3 || garr[1].y != 0 || garr[2].z != 99)
        return 6;
    if (go.n != 7 || go.in.x != 'b' || go.in.y != -1 || go.in.z != 5 || go.in.w != 6 ||
        go.flag != 1)
        return 7;
    if (lo.n != 7 || !same(&lo.in, &go.in) || lo.flag != 1)
        return 8;
    if (gu.i != -5 || lu.i != -5)
        return 9;
    if (gs.x || gs.y || gs.z || gs.w)
        return 10;
    return 0;
})"));
}

// Bit-fields reached every way: through a pointer, an array element, a member of a
// member, a returned struct; in a union with a plain member; and passed by value.
TEST_F(BookTest, Chapter18_BitfieldAccess)
{
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
struct flags {
    unsigned ready : 1;
    unsigned mode : 3;
    int level : 5;
};

struct device {
    char name;
    struct flags f[2];
};

union reg {
    struct flags f;
    unsigned char bits;
};

struct flags make(int mode, int level) {
    struct flags r;
    r.ready = 1;
    r.mode = mode;
    r.level = level;
    return r;
}

int sum(struct flags f) {
    return f.ready + f.mode + f.level;
}

int main(void) {
    struct device d;
    struct device *p = &d;
    d.name = 'D';
    for (int i = 0; i < 2; i++) {
        p->f[i].ready = i;
        p->f[i].mode = 5 + i;
        p->f[i].level = -3 - i;
    }
    if (d.f[0].ready != 0 || d.f[0].mode != 5 || d.f[0].level != -3)
        return 1;
    if (d.f[1].ready != 1 || d.f[1].mode != 6 || d.f[1].level != -4)
        return 2;
    if (d.name != 'D')
        return 3;
    if (make(3, -9).level != -9 || make(3, -9).mode != 3)
        return 4;
    if (sum(make(7, 15)) != 23 || sum(d.f[1]) != 3)
        return 5;
    struct flags copy = d.f[1];
    copy.mode++;
    if (copy.mode != 7 || d.f[1].mode != 6)
        return 6;
    union reg r;
    r.bits = 0;
    r.f.ready = 1;
    if (r.bits == 0)
        return 7;
    if (r.f.mode != 0 || r.f.ready != 1)
        return 8;
    return 0;
})"));
}
