//
// Unix-path run tests: compile → b6as → b6ld (crt0.o first) → b6sim, capturing the
// program's stdout.  Unlike the Madlen-on-dubna path — whose KOI7 output device folds
// text to upper case — the Unix (b6as) path is transparent: no KOI7 conversion, so
// bytes reach the host stdout verbatim and lower-case source text stays lower case.
// The expected strings therefore mirror the source text as written.  Task U6.
//
// Unlike the Madlen libc (whose startup calls `void program()`), the Unix crt0 calls
// `int main(void)` (see libc/besm6/unix/crt0.s), so these programs define main() and do
// their I/O directly; the printed stdout is what matters for the parity check.
//
// These need the sibling v7besm toolchain (b6as/b6ld/b6sim) on PATH; each test skips
// cleanly when it is absent, so `make run` stays green on machines without it.
//
#include "codegen_test.h"

TEST_F(CodegenTest, UnixRunEmptyProgram)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    EXPECT_EQ("", CompileAndRunUnix("int main(void) { return 0; }"));
}

TEST_F(CodegenTest, UnixRunPrintChar)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            putbyte('Q');
            putbyte('\n');
            return 0;
        }
    )");
    EXPECT_EQ("Q\n", result);
}

TEST_F(CodegenTest, UnixRunPrintDecimal)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            printf("%d\n", 42);
            return 0;
        }
    )");
    EXPECT_EQ("42\n", result);
}

// putchar writes one byte and returns it; print the byte then its returned value.
TEST_F(CodegenTest, UnixRunPutChar)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            int r = putchar('Q');
            putchar('\n');
            printf("%d\n", r);
            return 0;
        }
    )");
    EXPECT_EQ("Q\n81\n", result);
}

TEST_F(CodegenTest, UnixRunPrintFormatDecimal)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            printf("foo = %d, bar = %d\n", 123, -456);
            return 0;
        }
    )");
    EXPECT_EQ("foo = 123, bar = -456\n", result);
}

TEST_F(CodegenTest, UnixRunPrintFormatString)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            printf("hello %s\n", "world");
            return 0;
        }
    )");
    EXPECT_EQ("hello world\n", result);
}

TEST_F(CodegenTest, UnixRunPrintFormatChar)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            printf("hello %c%c%c%c%c\n", '(', '-', '_', '-', ')');
            return 0;
        }
    )");
    EXPECT_EQ("hello (-_-)\n", result);
}

// Exercise the multiply/divide/remainder runtime helpers end-to-end, printing the
// computed value.  20*7 + 20/7 - 20%7 = 140 + 2 - 6 = 136.
TEST_F(CodegenTest, UnixRunArithmetic)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) {
            int a = 20, b = 7;
            printf("%d\n", a * b + a / b - a % b);
            return 0;
        }
    )");
    EXPECT_EQ("136\n", result);
}

// The b6as counterpart of ZeroConstNeedsNoLiteralRun: a zero constant drops its `#`-pool
// operand on XTA, on the integer and FP `a+x`, and on XTS, and b6as/b6ld/b6sim accept and
// execute the resulting bare instructions.
TEST_F(CodegenTest, UnixRunZeroConstNeedsNoLiteral)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int add2(int a, int b) { return a + b; }
        int iplus0(int x) { return x + 0; }
        double dplus0(double v) { return v + 0.0; }
        int main(void) {
            printf("%d %d %d\n", iplus0(7), add2(7, 0), (int) dplus0(3.0));
            return 0;
        }
    )");
    EXPECT_EQ("7 7 3\n", result);
}

// Index a char array that the linker places at a high word address.  The b6as helpers
// keep their scratch and lookup tables in their own `.bss`/`.data`, which b6ld lays out
// after every object's data — so a large initialized array in the program pushes b$padd's
// `ttab` past word 07777 (4095), beyond the reach of a 12-bit short address field.  Before
// the `utc` escapes went in, b6as/b6ld silently masked those references to 12 bits: `ttab`
// linked at 047244 was read at 07244 (inside `pad[]`, reading zeros), so every fat pointer
// came back with offset_enc 0 and addressed byte #5 of the right word — digits[6] yielded
// 'b' instead of '6'.  `pad` must be *initialized* to land in `.data`: `.bss` follows libc's
// `.data`, so an uninitialized pad would leave `ttab` low and hide the bug.
TEST_F(CodegenTest, UnixRunCharPtrHighAddress)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int  pad[5000] = { 1 };
        char digits[] = "0123456789abcdef";
        int main(void) {
            volatile int i;
            for (i = 0; i < 16; i++)
                putchar(digits[i]);
            putchar('\n');
            return 0;
        }
    )");
    EXPECT_EQ("0123456789abcdef\n", result);
}

// The address of a global rides in VTM's own 15-bit address field (`14 vtm g`), where it
// used to need a preceding `utc g`.  VTM had never carried a relocatable operand before,
// so this checks that b6as and b6ld relocate an external name in a VTM word exactly as
// they do in a UTC word — for a plain global, an array element, and a packed char member.
TEST_F(CodegenTest, UnixRunAddressOfGlobal)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct S { int a; char c; };
        int g;
        int arr[3];
        struct S s;
        int main(void) {
            int *p = &g;
            *p = 42;
            int *q = &arr[2];
            *q = 7;
            s.c = 'Q';
            printf("%d %d %d %c\n", g, *p, arr[2], s.c);
            return 0;
        }
    )");
    EXPECT_EQ("42 42 7 Q\n", result);
}

// End-to-end through the real toolchain: a string literal with an embedded NUL keeps
// every byte.  Both the static copy (data words emitted by the backend) and the local
// copy (byte stores emitted by the translator) must hold 'a', NUL, 'c', NUL — the
// literal used to be cut short at the decoded NUL and arrive as the single byte 'a'.
TEST_F(CodegenTest, UnixRunStringWithEmbeddedNul)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        char g[] = "a\0c";
        int main(void) {
            char s[] = "a\0c";
            printf("%d %d %d %d\n", g[0], g[1], g[2], g[3]);
            printf("%d %d %d %d\n", s[0], s[1], s[2], s[3]);
            printf("%d %d\n", (int)sizeof g, (int)sizeof "a\0c");
            return 0;
        }
    )");
    EXPECT_EQ("97 0 99 0\n97 0 99 0\n4 4\n", result);
}

// End-to-end: an assignment used as a VALUE must yield the value stored.  This is v7's
// callout compaction loop (kernel/clock.c) in miniature: the loop condition IS the store,
// and it used to branch on an undefined frame slot, so the loop walked off the end of the
// table and never came back.  The two scalar forms below fail the same way.
TEST_F(CodegenTest, UnixRunAssignmentAsValue)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct s { int a, b; } S;
        int src[4] = { 3, 4, 5, 0 };
        int dst[4];
        int main(void) {
            int *p = dst, *q = src;
            int n = 0;
            while ((*p = *q)) { p++; q++; n++; }
            printf("%d %d %d %d\n", n, dst[0], dst[1], dst[2]);
            int t = (dst[0] = 9);
            printf("%d\n", t);
            printf("%d %d\n", (S.b = 0), (S.b = 6));
            return 0;
        }
    )");
    EXPECT_EQ("3 3 4 5\n9\n0 6\n", result);
}

// End-to-end: a truth test on an additive result.  This is backend/besm6/tmp/BUG.md's
// repro, found while porting v7's sort(1) — `if (b = *--ipb - *--ipa)` is true exactly when
// the first key sorts before the second, and that is the half a *sign* test throws away.
// Without rule #33's ω fixup, `if (x - y)` inherits the additive ω of the `a-x` and reads
// as `if (x - y >= 0)`, so f(5,4) answers 0 and g(1,0) answers 0.
TEST_F(CodegenTest, UnixRunTruthTestOfAdditiveResult)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        int f(int x, int y) { if (x - y) return 1; return 0; }
        int g(int x, int y) { if (x + y) return 1; return 0; }
        int h(int x)        { int b = -x; if (b) return 1; return 0; }
        int t(int x, int y) { int b = x - y; return b ? 1 : 0; }
        int a(int x, int y) { int b = x - y; return b && 1; }
        int o(int x, int y) { int b = x - y; return b || 0; }
        int main(void) {
            printf("%d%d%d\n", f(5, 4), f(4, 5), f(4, 4));
            printf("%d%d%d\n", g(1, 0), g(-1, 0), g(0, 0));
            printf("%d%d\n",   h(1), h(0));
            printf("%d%d%d\n", t(5, 4), a(5, 4), o(5, 4));
            return 0;
        }
    )");
    EXPECT_EQ("110\n110\n10\n111\n", result);
}

// The same test one level of indirection down: the condition IS the assignment, which is
// how v7's sort spells it.  The store must not disturb the ω the branch reads, and the
// value tested must be the difference, not its sign.
TEST_F(CodegenTest, UnixRunAssignedDifferenceAsCondition)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        char ka[] = "31";
        char kb[] = "21";
        int cmp(char *pa, char *pb, int n) {
            char *ipa = pa + n, *ipb = pb + n;
            int a = 0, b;
            while (ipa > pa && ipb > pb)
                if ((b = *--ipb - *--ipa))
                    a = b;
            return a;
        }
        int main(void) {
            printf("%d %d %d\n", cmp(ka, kb, 2), cmp(kb, ka, 2), cmp(ka, ka, 2));
            return 0;
        }
    )");
    EXPECT_EQ("-1 1 0\n", result);
}

// Compound literals as lvalues, and sizeof of a literal, on the b6as path.
TEST_F(CodegenTest, UnixRunCompoundLiteralLvalue)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct s { int a, b; };
        int main(void) {
            int *p = &(int){ 5 };
            struct s *q = &(struct s){ 1 };
            char *c = &(char){ 'A' };
            q->b = 7;
            *c = 'B';
            printf("%d %d %d %c %d\n", *p, q->a, q->b, *c, ++(int){ 0 });
            printf("%d %d\n", (int)sizeof (int[3]){ 0 }, (int)sizeof (struct s){ 1 }.b);
            return 0;
        }
    )");
    EXPECT_EQ("5 1 7 B 1\n18 6\n", result);
}

// File-scope compound literals on the b6as path.
TEST_F(CodegenTest, UnixRunFileScopeCompoundLiteral)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct s { int a; int b; char *n; };
        int *p = (int[]){ 1, 2, 3 } + 1;
        struct s *q = &(struct s){ 7, 8, "AB" };
        char *c = &(char){ 'C' };
        int **pp = (int *[]){ (int[]){ 14 }, 0 };
        int main(void) {
            q->a += 100;
            p[1] = 15;
            printf("%d %d %d %s %c %d\n", p[0], p[1], q->a, q->n, *c, pp[0][0]);
            return 0;
        }
    )");
    EXPECT_EQ("2 15 107 AB C 14\n", result);
}

// Bulk zero fill on the b6as path, over a dirtied stack.
TEST_F(CodegenTest, UnixRunBulkZeroFill)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct m { char c; int x; char d[5]; int y[10]; };
        static void dirty(void) {
            int junk[100];
            for (int i = 0; i < 100; i++)
                junk[i] = -1;
            printf("%d", junk[3] & 0);
        }
        static void test(void) {
            int a[50] = { [49] = 1 };
            struct m s = { 'A', .y[9] = 3 };
            int n = 0;
            for (int i = 0; i < 50; i++)
                n += a[i];
            for (int i = 0; i < 10; i++)
                n += s.y[i];
            printf(" %d %c %d %d %d\n", n, s.c, s.x, s.d[4], a[0]);
        }
        int main(void) { dirty(); test(); return 0; }
    )");
    EXPECT_EQ("0 4 A 0 0 0\n", result);
}

// Char arrays from strings with a bulk-zeroed tail, on the b6as path.
TEST_F(CodegenTest, UnixRunBulkZeroFillString)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        static void dirty(void) {
            int junk[100];
            for (int i = 0; i < 100; i++)
                junk[i] = -1;
            printf("%d", junk[3] & 0);
        }
        static void test(void) {
            char a[60] = "AB";
            char m[3][20] = { "CD", "E" };
            char *c = (char[30]){ "FG" };
            int z = 0;
            for (int i = 0; i < 60; i++)
                z += a[i] == 0;
            for (int i = 0; i < 30; i++)
                z += c[i] == 0;
            printf(" %s %s %s %s %d %d\n", a, m[0], m[1], c, m[2][0], z);
        }
        int main(void) { dirty(); test(); return 0; }
    )");
    EXPECT_EQ("0 AB CD E FG 0 86\n", result);
}

// Packed zero words and merged zero runs assemble to the right layout.
TEST_F(CodegenTest, UnixRunZeroRuns)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct p { int a, b, c; };
        struct p t[4] = { { 1 }, { 0 }, { 0, 0, 3 } };
        struct m { char c; int x; int y[3]; } v = { 1, 0 };
        int u[12] = { 0, 0, 5, [11] = 6 };
        int main(void) {
            int s = 0;
            for (int i = 0; i < 4; i++)
                s = s * 10 + t[i].a + t[i].b + t[i].c;
            printf("%d %d %d %d %d %d %d\n", s, v.c, v.x, v.y[2], u[2], u[10], u[11]);
            return 0;
        }
    )");
    EXPECT_EQ("1030 1 0 0 5 0 6\n", result);
}

// A multi-word struct value read through memory (*p, s.m, a[i], p->m) and used as a
// value — returned, passed, or a ?: arm — is copied whole, not loaded as one word.
TEST_F(CodegenTest, UnixRunStructValueThroughMemory)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        struct T { long a, b, c; };
        struct S { long pad; struct T in; } gs = { 9, { 4, 5, 6 } };
        struct T arr[2] = { { 7, 8, 9 }, { 1, 1, 1 } };
        struct T id(struct T *p) { return *p; }
        long get(struct T t) { return t.a * 100 + t.b * 10 + t.c; }
        int main(void) {
            struct T x = { 1, 2, 3 };
            struct S *ps = &gs;
            struct T y = id(&x);
            printf("%ld %ld %ld\n", y.a, y.b, y.c);
            printf("%ld %ld %ld %ld\n", get(*&x), get(gs.in), get(arr[0]), get(ps->in));
            printf("%ld\n", get(y.a ? arr[0] : gs.in));
            return 0;
        }
    )");
    EXPECT_EQ("1 2 3\n123 456 789 456\n789\n", result);
}

// A one-word struct returned by a call lands in a temporary whose member is then loaded
// through its address: the store of the result must not be dropped as a dead store.
TEST_F(CodegenTest, UnixRunMemberOfReturnedStruct)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    EXPECT_EQ("42\n", CompileAndRunUnix(R"(
        #include <stdio.h>
        struct plain { int a; };
        struct plain mk(int a) { struct plain r; r.a = a; return r; }
        int main(void) { printf("%d\n", mk(42).a); return 0; }
    )"));
}

// A folded operation takes its result's type: `unsigned u = 5` copy-propagates an int 5,
// and 5 << 45 must keep all 48 bits of the unsigned word, not an int's 41.
TEST_F(CodegenTest, UnixRunFoldUnsignedWideShift)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    EXPECT_EQ("5000000000000000\n", CompileAndRunUnix(R"(
        #include <stdio.h>
        int main(void) { unsigned u = 5; unsigned v = u << 45; printf("%o\n", v); return 0; }
    )"));
}
