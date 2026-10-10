#include "codegen_test.h"

TEST_F(CodegenTest, FuncArg1)
{
    std::string output = CompileToMadlen(R"(
        void foo(int bar);
        void quz() {
            foo(42);
        }
    )");
    EXPECT_EQ(R"(c
      quz:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta, =52
          14 ,vtm, -1
             ,call, foo
             ,uj, b/ret
             ,end,
)",
              output);
}

TEST_F(CodegenTest, RunRev)
{
    char in_path[] = "/tmp/rev_input_XXXXXX";
    int in_fd      = mkstemp(in_path);
    ASSERT_GE(in_fd, 0);
    const char *input = "hello\nworld\nfoo\n";
    ASSERT_GT(write(in_fd, input, strlen(input)), 0);
    close(in_fd);

    char out_path[] = "/tmp/rev_output_XXXXXX";
    int out_fd      = mkstemp(out_path);
    ASSERT_GE(out_fd, 0);
    close(out_fd);

    RunExternalProgram("rev", { in_path }, out_path);

    FILE *f = fopen(out_path, "r");
    ASSERT_NE(nullptr, f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    std::string got(static_cast<size_t>(len), '\0');
    ASSERT_NE(fread(&got[0], 1, static_cast<size_t>(len), f), NULL);
    fclose(f);

    EXPECT_EQ(got, "olleh\ndlrow\noof\n");

    unlink(in_path);
    unlink(out_path);
}

TEST_F(CodegenTest, EmptyProgram)
{
    std::string result = CompileAndRun("void program() {}");
    EXPECT_EQ("", result);
}

TEST_F(CodegenTest, PrintChar)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            putbyte('Q');
            putbyte('\n');
        }
    )");
    EXPECT_EQ("Q\n", result);
}

// putchar writes one byte and returns it; print the byte then its returned value.
TEST_F(CodegenTest, PutChar)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            int r = putchar('Q');
            putchar('\n');
            printf("%d\n", r);
        }
    )");
    EXPECT_EQ("Q\n81\n", result);
}

TEST_F(CodegenTest, PrintDecimal)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            printf("%d\n", 42);
        }
    )");
    EXPECT_EQ("42\n", result);
}

// Multi-character constants: a static global initializer (exercising the
// INIT_I64 static-data path) and an inline argument, both byte-packed.
//   'ab' = 0x6162 = 24930,  'cd' = 0x6364 = 25444
TEST_F(CodegenTest, MultiCharConstant)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int g = 'ab';
        void program() {
            printf("%d\n", g);
            printf("%d\n", 'cd');
        }
    )");
    EXPECT_EQ("24930\n25444\n", result);
}

TEST_F(CodegenTest, PrintTwoLines)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            printf("First line.\nSecond line.\n");
        }
    )");
    EXPECT_EQ("FIRST LINE.\nSECOND LINE.\n", result);
}

// End-to-end: a parameterless _Noreturn function (no b/save0 prologue) runs correctly
// on the simulator — it prints, then tail-calls _Noreturn exit().
TEST_F(CodegenTest, NoreturnNoParamsRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <stdlib.h>
        _Noreturn void die(void) {
            printf("BYE\n");
            exit(0);
        }
        void program() {
            printf("HELLO\n");
            die();
        }
    )");
    // die() runs with the elided prologue (no b/save0), prints, then tail-calls exit().
    // exit() halts the job mid-stream, so the monitor's "*END FILE" marker is captured
    // in the listing (a normal return ends after the "----" separator and is truncated).
    EXPECT_EQ("HELLO\nBYE\n\n*END FILE\n", result);
}

// Float constant in a function-call argument.
// 1.5 as double → "=r1.5" literal.
TEST_F(CodegenTest, FuncArgFloat)
{
    std::string output = CompileToMadlen(R"(
        void foo(double x);
        void quz(void) { foo(1.5); }
    )");
    EXPECT_EQ(R"(c
      quz:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta, =r1.5
          14 ,vtm, -1
             ,call, foo
             ,uj, b/ret
             ,end,
)",
              output);
}

TEST_F(CodegenTest, PrintFormatDecimal)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            printf("foo = %d, bar = %d\n", 123, -456);
        }
    )");
    EXPECT_EQ("FOO = 123, BAR = -456\n", result);
}

TEST_F(CodegenTest, PrintFormatOctal)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            printf("foo = %o, bar = %o\n", 123, -456);
        }
    )");
    EXPECT_EQ("FOO = 173, BAR = 37777777777070\n", result);
}

TEST_F(CodegenTest, PrintFormatString)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            printf("hello %s\n", "world");
        }
    )");
    EXPECT_EQ("HELLO WORLD\n", result);
}

TEST_F(CodegenTest, PrintFormatChar)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            printf("hello %c%c%c%c%c\n", '(', '-', '_', '-', ')');
        }
    )");
    EXPECT_EQ("HELLO (-_-)\n", result);
}

// A char*/void* null must test as false even when it carries the fat-pointer marker.
// Casting a null int* to char* (PTR_TO_CHAR_PTR) ORs in the marker bit, so the stored
// word is 0x6400000000000000 rather than all-zero.  Null tests (truthiness, ==/!=, !)
// must compare only the word-address part, so the marker-tagged null still reads as
// null; a real pointer (nonzero address, any byte offset) must read as non-null.
TEST_F(CodegenTest, FatPointerNullTest)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int *ret_null(void) { return 0; }   // null int*, opaque to the optimizer
        void program() {
            char buf[6];
            char *nul = (char*)ret_null();   // marker-tagged null fat pointer
            char *p0  = buf;                 // real pointer, byte offset 0
            char *p3  = &buf[3];             // real pointer, byte offset within word
            printf("%c%c%c%c%c%c%c%c\n",
                nul ? 'T' : 'F', (nul == 0) ? 'E' : 'N', (nul != 0) ? 'N' : 'E', !nul ? 'Y' : 'F',
                p0 ? 'T' : 'F', (p0 == 0) ? 'E' : 'N',
                p3 ? 'T' : 'F', (p3 == 0) ? 'E' : 'N');
        }
    )");
    EXPECT_EQ("FEEYTNTN\n", result);
}

// Passing a null as a char*/void* argument must arrive as null in the callee.  A literal
// 0 / NULL is a null-pointer constant (lowered to COPY 0, all-zero), but passing a null
// int* to a void* parameter goes through the implicit int*->void* conversion
// (PTR_TO_CHAR_PTR), which marker-tags the null (0x6400000000000000) -- the same shape as
// free(nullintptr).  The callee's null test must still see it as null.
TEST_F(CodegenTest, FatPointerNullArgument)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int *ret_null(void) { return 0; }      // null int*, opaque to the optimizer
        char check_charp(char *p) { return p ? 'T' : 'F'; }
        char check_voidp(void *p) { return (p == 0) ? 'E' : 'N'; }
        void program() {
            char buf[6];
            char a = check_charp(0);            // literal 0 -> char* param
            char b = check_voidp(0);            // literal 0 -> void* param
            char c = check_voidp(ret_null());   // null int* -> void* (marker-tagged)
            char d = check_charp(buf);          // real pointer -> char* param
            printf("%c%c%c%c\n", a, b, c, d);
        }
    )");
    EXPECT_EQ("FEET\n", result);
}

TEST_F(CodegenTest, MallocHeapRoundTrip)
{
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        #include <stdlib.h>
        #include <malloc.h>

        int main() {
            long avail0 = malloc_free_bytes();

            int *a = malloc(10 * sizeof(int));
            int *b = malloc(5 * sizeof(int));
            for (int i = 0; i < 10; i++) a[i] = i * i;
            int sum = 0;
            for (int i = 0; i < 10; i++) sum += a[i];
            printf("SUM %d\n", sum);

            free(a);
            int *c = malloc(8 * sizeof(int));
            printf("REUSE %d\n", c == a);

            free(b);
            free(c);
            printf("FULL %d\n", malloc_free_bytes() == avail0);

            int *d = calloc(4, sizeof(int));
            printf("ZERO %d\n", d[0] + d[1] + d[2] + d[3]);
            d = realloc(d, 12 * sizeof(int));
            d[11] = 7;
            printf("REALLOC %d\n", d[11]);
            free(d);
        }
    )");
    EXPECT_EQ("SUM 285\nREUSE 1\nFULL 1\nZERO 0\nREALLOC 7\n", result);
}

// main() falling off the end gets an implicit `return 0;` (C11 §5.1.2.2.3), so
// its return value is a well-defined 0 rather than indeterminate accumulator
// garbage.  program() prints main()'s value.
TEST_F(CodegenTest, MainImplicitReturnZero)
{
    std::string result = CompileAndRun(
        "int printf(const char *format, ...);\n"
        "int main(void) { }\n"
        "void program(void) { printf(\"%d\\n\", main()); }\n");
    EXPECT_EQ("0\n", result);
}

// Same, with a non-empty body that still falls off the end.
TEST_F(CodegenTest, MainImplicitReturnZeroNonEmptyBody)
{
    std::string result = CompileAndRun(
        "int printf(const char *format, ...);\n"
        "int main(void) { int a = 3; a = a + 5; }\n"
        "void program(void) { printf(\"%d\\n\", main()); }\n");
    EXPECT_EQ("0\n", result);
}

// A local whose struct/union tag is declared *inside* the function body (BESM-6 backend
// TODO #62).  The tag is purged from structtab on block exit, so the translator can no
// longer resolve the type's size/alignment; validate_type caches both on the AST node while
// the tag is live, clone_type carries the cache into every copy of that node, and
// get_size/get_alignment fall back to it.  (That is one of three things the translator needs
// past the purge -- the others are the member offset and the member's declared type, both
// cached on the access node; see LocalBlockStructCharArrayIndexed below.)  Here dtoi
// reinterprets the bits of the double 1.0 as an int, and "%o" prints that word, so the
// expected output is the octal of 1.0's representation.
TEST_F(CodegenTest, LocalBlockUnionType)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int dtoi(double d) {
            union u {
                double d;
                int i;
            };
            union u x;
            x.d = d;
            return x.i;
        }
        void program() {
            printf("%o\n", dtoi(1.0));
        }
    )");
    EXPECT_EQ("4050000000000000\n", result);
}

// A `static` local whose struct tag is declared inside the function body (TODO #62, the
// path the StaticLocalRec borrows a Type* through).  The static struct keeps its value
// across calls; each call prints the previous {a,b} then stores the new one.
TEST_F(CodegenTest, StaticLocalBlockStructType)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void bump(int a, int b) {
            struct s {
                int a;
                int b;
            };
            static struct s acc;
            printf("%d %d\n", acc.a, acc.b);
            acc.a = a;
            acc.b = b;
        }
        void program() {
            bump(1, 2);
            bump(3, 4);
            bump(5, 6);
        }
    )");
    EXPECT_EQ("0 0\n1 2\n3 4\n", result);
}

// An *automatic* local struct with a *compound initializer*, where the tag is declared
// inside the function body (TODO #63).  gen_compound_init used to re-query structtab for the
// member offsets, but the block-local tag is purged on block exit; the offsets are now cached
// on each InitItem by typecheck instead.
TEST_F(CodegenTest, AutoLocalBlockStructCompoundInit)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            struct s {
                int a;
                int b;
            };
            struct s x = { 1, 2 };
            printf("%d %d\n", x.a, x.b);
        }
    )");
    EXPECT_EQ("1 2\n", result);
}

// Same, but with a nested struct member, to exercise gen_compound_init's recursion through
// cached offsets at more than one level.
TEST_F(CodegenTest, AutoLocalBlockStructNestedCompoundInit)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            struct inner {
                int p;
                int q;
            };
            struct outer {
                int a;
                struct inner in;
                int b;
            };
            struct outer x = { 1, { 2, 3 }, 4 };
            printf("%d %d %d %d\n", x.a, x.in.p, x.in.q, x.b);
        }
    )");
    EXPECT_EQ("1 2 3 4\n", result);
}

// Partial compound init of an automatic block-local nested struct: trailing members and
// trailing members of a partially-given nested struct must zero-fill.  Exercises the
// zero-fill arm of typecheck_init / gen_compound_init, whose InitItems also carry cached
// member offsets.
TEST_F(CodegenTest, AutoLocalBlockStructPartialCompoundInit)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            struct inner {
                int p;
                int q;
                int r;
            };
            struct outer {
                int a;
                struct inner in;
                int b;
            };
            struct outer x = { 1, { 2 } };
            printf("%d %d %d %d %d\n", x.a, x.in.p, x.in.q, x.in.r, x.b);
        }
    )");
    EXPECT_EQ("1 2 0 0 0\n", result);
}

// An *array* of a block-local struct, indexed -- the shape of the trigraph table in the
// v7 preprocessor's buffer.c.  The ADD_PTR element stride comes from get_size() of the
// decayed pointer's target, a *clone* of the declarator's element type, and clone_type
// used to drop the size/alignment cache that validate_type had left there, so this aborted
// with "use of undeclared tag '__anon_1'".
TEST_F(CodegenTest, LocalBlockStructCharArrayIndexed)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            static const struct { char t, r; } map[] = { { 'A', 'B' }, { 'C', 'D' } };
            int i;
            for (i = 0; i < 2; i++)
                printf("%c%c\n", map[i].t, map[i].r);
        }
    )");
    EXPECT_EQ("AB\nCD\n", result);
}

// Whole-aggregate assignment and sizeof of a block-local struct: both re-derive the
// aggregate's size from a cloned type node, so both hit the same dropped cache.
TEST_F(CodegenTest, LocalBlockStructSizeofAndAssign)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            struct s { int a, b, c; };
            struct s m = { 1, 2, 3 };
            struct s n;
            n = m;
            printf("%d %d %d %d\n", n.a, n.b, n.c, (int)sizeof(m));
        }
    )");
    EXPECT_EQ("1 2 3 18\n", result);
}

// A byte member of a block-local struct reached through a pointer.  The member's declared
// type decides whether the base is retyped to a fat byte pointer before the scale-1 member
// offset; with the tag purged and no cached member type, the offset was applied to a plain
// word address (which the byte helpers read as byte #5) and the wrong byte was addressed.
TEST_F(CodegenTest, LocalBlockStructPtrCharMember)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            struct s { char t, r; };
            struct s m;
            struct s *p = &m;
            p->t = 'X';
            p->r = 'Z';
            printf("%c%c\n", p->t, p->r);
        }
    )");
    EXPECT_EQ("XZ\n", result);
}

// End-to-end: a zero constant operand is emitted with an empty address field, so the
// instruction reads memory word 0 (architecturally zero) instead of a reserved literal.
// This exercises the three sites that attach a structural constant — XTA, the arith ops
// (integer `a+x` and the ntr-bracketed FP `a+x`), and XTS (a call argument) — and proves
// Madlen accepts a bare address field on each of them.
TEST_F(CodegenTest, ZeroConstNeedsNoLiteralRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int add2(int a, int b) { return a + b; }
        int iplus0(int x) { return x + 0; }
        double dplus0(double v) { return v + 0.0; }
        void program() {
            printf("%d %d %d\n", iplus0(7), add2(7, 0), (int) dplus0(3.0));
        }
    )");
    EXPECT_EQ("7 7 3\n", result);
}

// End-to-end: static pointers initialized with composed C11 §6.6 address constants must
// point at the right word at run time, not merely compile.  `&arr[1] + 1` == &arr[2] (30);
// `&s.v[2]` selects the third element of the array member (300).  Proves the byte offset
// build_static_init folds is arithmetically correct after the backend's divide-by-word.
TEST_F(CodegenTest, StaticAddressConstantRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int arr[3] = { 10, 20, 30 };
        int *p = &arr[1] + 1;
        struct S { int a; int v[3]; } s = { 7, { 100, 200, 300 } };
        int *q = &s.v[2];
        void program() {
            printf("%d %d\n", *p, *q);
        }
    )");
    EXPECT_EQ("30 300\n", result);
}

// ---------------------------------------------------------------------------
// Comma operator (C11 6.5.17) — end-to-end
//
// Regression for the bug where parse_expression() hung the right operand off
// Expr->next (the argument-list sibling link, which nothing reads on an ordinary
// expression) and returned the *left* operand: `(a, b)` evaluated to `a` and `b`
// was never evaluated at all.  Silent — no diagnostic, wrong code.
// ---------------------------------------------------------------------------

// The value of a comma expression is the right operand's, and every operand is
// evaluated, left to right.  Covers the reproducer from the bug report: plain
// `(a,b)`, a two- and a three-operand chain of calls, both `for` clauses, and a
// bare comma expression statement.
TEST_F(CodegenTest, CommaOperatorRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int comtrc;
        int comfa(void) { comtrc = comtrc * 10 + 1; return 7; }
        int comfb(void) { comtrc = comtrc * 10 + 2; return 9; }
        void program() {
            int a, b, i, j, r;

            a = 5; b = 0;
            printf("%d\n", (a, b));                 /* 0 */

            comtrc = 0; r = (comfa(), comfb());
            printf("%d %d\n", r, comtrc);           /* 9 12 */

            comtrc = 0; r = (comfa(), comfb(), 5);
            printf("%d %d\n", r, comtrc);           /* 5 12 */

            comtrc = 0; (void)(comfa(), comfb());
            printf("%d\n", comtrc);                 /* 12 */

            i = 99; j = 99;
            for (i = 0, j = 7; i < 1; i++)
                ;
            printf("%d\n", j);                      /* 7 */

            i = 0; j = 0;
            for (r = 0; r < 3; i++, j += 2)
                r++;
            printf("%d\n", j);                      /* 6 */

            i = 1; j = 1;
            i++, j++;
            printf("%d\n", j);                      /* 2 */
        }
    )");
    EXPECT_EQ("0\n9 12\n5 12\n12\n7\n6\n2\n", result);
}

// Every context that consumes the value of a comma expression must see the right
// operand.  Before the fix each one of these took the left operand's value.
TEST_F(CodegenTest, CommaOperatorContextsRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            int a, b, x, y, n;
            double d;

            a = 5; b = 0; x = 11; y = 22;

            n = 0;
            if ((a, b)) n = 1;
            printf("%d\n", n);                      /* 0 — b is false */

            n = 0; b = 3;
            while ((a, b)) { n++; b--; }
            printf("%d\n", n);                      /* 3 — and it terminates */

            b = 0;
            printf("%d\n", (a, b) ? x : y);         /* 22 */
            printf("%d\n", !(a, b));                /* 1 */

            b = 2;
            printf("%d\n", (a, b) + 100);           /* 102 */

            x = (1, 2, 3);
            printf("%d\n", x);                      /* 3 */

            /* The type is the right operand's, after lvalue conversion. */
            printf("%d %d\n", (int)(sizeof(a, d) == sizeof(double)),
                              (int)(sizeof(d, a) == sizeof(int)));   /* 1 1 */
        }
    )");
    EXPECT_EQ("0\n3\n22\n1\n102\n3\n1 1\n", result);
}

// The idiom that exposed the bug: v7's atof() is written around
// `while ((c = *p++), isdigit(c))`.  With the right operand dropped the loop
// tested the assignment's value instead of the predicate and never terminated.
TEST_F(CodegenTest, CommaOperatorScanLoopRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            char *p;
            int c, n;

            p = "123X";
            n = 0;
            while ((c = *p++), (c >= '0' && c <= '9'))
                n = n * 10 + (c - '0');
            printf("%d %c\n", n, c);                /* 123 X */
        }
    )");
    EXPECT_EQ("123 X\n", result);
}

// The Madlen/dubna half of the ω-fixup behavior guard (see the Unix run tests for the
// full repro from backend/besm6/tmp/BUG.md).  Every digit here is a truth test on a value
// the code generator leaves in the accumulator straight out of an additive instruction:
// without rule #33 each one reads as `>= 0` and the first, fourth and sixth flip.
TEST_F(CodegenTest, TruthTestOfAdditiveResultRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int f(int x, int y) { if (x - y) return 1; return 0; }
        int g(int x, int y) { if (x + y) return 1; return 0; }
        int h(int x)        { int b = -x; if (b) return 1; return 0; }
        void program() {
            printf("%d%d%d\n", f(5, 4), f(4, 5), f(4, 4));
            printf("%d%d%d\n", g(1, 0), g(-1, 0), g(0, 0));
            printf("%d%d\n",   h(1), h(0));
        }
    )");
    EXPECT_EQ("110\n110\n10\n", result);
}

// Brace elision (C11 §6.7.9p20) for an array of structs with a char * member, static
// and automatic: "AB", 1 fill tab[0]; the element count comes from the rows filled.
TEST_F(CodegenTest, BraceElisionStructArray)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { char *name; int v; };
        struct s g[] = { "AB", 1, "CD", 2 };
        void program() {
            struct s a[2] = { "EF", 3, "GH" };
            printf("%d %s %d %s %d\n", (int)(sizeof g / sizeof g[0]), g[0].name, g[0].v,
                   g[1].name, g[1].v);
            printf("%s %d %s %d\n", a[0].name, a[0].v, a[1].name, a[1].v);
        }
    )");
    EXPECT_EQ("2 AB 1 CD 2\nEF 3 GH 0\n", result);
}

// Brace elision with a char array member: the string fills the member, not its chars.
TEST_F(CodegenTest, BraceElisionCharArrayMember)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { char n[4]; int v; };
        struct s g[] = { "AB", 1, "CD", 2 };
        void program() {
            struct s a[2] = { "EF", 3, "GH", 4 };
            printf("%s %d %s %d\n", g[0].n, g[0].v, g[1].n, g[1].v);
            printf("%s %d %s %d\n", a[0].n, a[0].v, a[1].n, a[1].v);
        }
    )");
    EXPECT_EQ("AB 1 CD 2\nEF 3 GH 4\n", result);
}

// Brace elision in a 2-D array, and at the boundary of an inner array member.
TEST_F(CodegenTest, BraceElisionMatrix)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int g[2][2] = { 1, 2, 3 };
        struct s { int a[2]; int b; };
        struct s gs = { 4, 5, 6 };
        void program() {
            int m[2][2] = { 7, 8, 9 };
            struct s x = { { 1 }, 2 };
            printf("%d %d %d %d\n", g[0][0], g[0][1], g[1][0], g[1][1]);
            printf("%d %d %d\n", gs.a[0], gs.a[1], gs.b);
            printf("%d %d %d %d\n", m[0][0], m[0][1], m[1][0], m[1][1]);
            printf("%d %d %d\n", x.a[0], x.a[1], x.b);
        }
    )");
    EXPECT_EQ("1 2 3 0\n4 5 6\n7 8 9 0\n1 0 2\n", result);
}

// A braced scalar initializer (C11 §6.7.9p11), static and automatic.
TEST_F(CodegenTest, BracedScalarInit)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        int g = { 5 };
        char *p = { "AB" };
        void program() {
            int x = { 6 };
            static int y = { 7 };
            printf("%d %s %d %d\n", g, p, x, y);
        }
    )");
    EXPECT_EQ("5 AB 6 7\n", result);
}

// An automatic struct element initialized from a struct value is not brace-elided.
TEST_F(CodegenTest, BraceElisionStructValue)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int a; int b; };
        void program() {
            struct s v = { 1, 2 };
            struct s a[2] = { v, 3, 4 };
            printf("%d %d %d %d\n", a[0].a, a[0].b, a[1].a, a[1].b);
        }
    )");
    EXPECT_EQ("1 2 3 4\n", result);
}

// Field designators (C11 §6.7.9p17), static and automatic: out of order, positional
// continuation after a designator, and a repeated member (the later one wins).
TEST_F(CodegenTest, FieldDesignators)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int a, b, c; };
        struct s g1 = { .c = 3, .a = 1 };
        struct s g2 = { .b = 1, 2 };
        struct s g3 = { .a = 1, 2, .a = 3 };
        void program() {
            struct s x1 = { .c = 3, .a = 1 };
            struct s x2 = { .b = 1, 2 };
            struct s x3 = { .a = 1, 2, .a = 3 };
            printf("%d %d %d %d %d %d %d %d %d\n", g1.a, g1.b, g1.c, g2.a, g2.b, g2.c,
                   g3.a, g3.b, g3.c);
            printf("%d %d %d %d %d %d %d %d %d\n", x1.a, x1.b, x1.c, x2.a, x2.b, x2.c,
                   x3.a, x3.b, x3.c);
        }
    )");
    EXPECT_EQ("1 0 3 0 1 2 3 2 0\n1 0 3 0 1 2 3 2 0\n", result);
}

// A designated char * member from a string literal, static and automatic.
TEST_F(CodegenTest, FieldDesignatorStringMember)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int v; char *name; };
        struct s g = { .name = "AB" };
        void program() {
            struct s x = { .name = "CD", .v = 2 };
            printf("%d %s %d %s\n", g.v, g.name, x.v, x.name);
        }
    )");
    EXPECT_EQ("0 AB 2 CD\n", result);
}

// A union initialized through a non-first member, static and automatic.
TEST_F(CodegenTest, UnionFieldDesignator)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        union u { int i; char *p; double d; };
        union u g = { .p = "AB" };
        union u h = { .d = 1.5 };
        void program() {
            union u x = { .p = "CD" };
            union u y = { .d = 2.5 };
            printf("%s %s %d %d\n", g.p, x.p, (int)(h.d * 2), (int)(y.d * 2));
        }
    )");
    EXPECT_EQ("AB CD 3 5\n", result);
}

// Designators into block-scope struct and union tags, and one ending brace elision.
TEST_F(CodegenTest, FieldDesignatorBlockScopeTags)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            struct in { int a, b; };
            struct s { struct in in; int c; char *name; };
            union u { int i; char *p; };
            struct s x = { 1, .name = "AB" };
            static struct s y = { .c = 3, .in = { 4, 5 } };
            union u z = { .p = "CD" };
            printf("%d %d %d %s\n", x.in.a, x.in.b, x.c, x.name);
            printf("%d %d %d\n", y.in.a, y.in.b, y.c);
            printf("%s\n", z.p);
        }
    )");
    EXPECT_EQ("1 0 0 AB\n4 5 3\nCD\n", result);
}

// Array designators (C11 §6.7.9p17), static and automatic: positional continuation, an
// unsized array sized by its highest index, an enumerator index, and char * elements.
TEST_F(CodegenTest, ArrayDesignators)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        enum { ONE = 1, TWO };
        int g[5] = { [3] = 7, 8, [1] = 2 };
        int big[] = { [9] = 1 };
        int e[3] = { [TWO] = 5, [ONE] = 4 };
        char *names[] = { [2] = "C", [0] = "A" };
        void program() {
            int a[5] = { [3] = 7, 8, [1] = 2 };
            char *n[] = { [1] = "Y", [0] = "X" };
            int i;
            for (i = 0; i < 5; i++)
                printf("%d%d ", g[i], a[i]);
            printf("\n%d %d %d %d\n", (int)sizeof big, (int)(sizeof big / sizeof big[0]),
                   big[9], big[8]);
            printf("%d %d %d\n", e[0], e[1], e[2]);
            printf("%d %s %d %s\n", (int)(sizeof names / sizeof names[0]), names[0],
                   names[1] == 0, names[2]);
            printf("%d %s %s\n", (int)(sizeof n / sizeof n[0]), n[0], n[1]);
        }
    )");
    EXPECT_EQ("00 22 00 77 88 \n60 10 1 0\n0 4 5\n3 A 1 C\n2 X Y\n", result);
}

// Designator chains (C11 §6.7.9p17-p19), static and automatic: a chain into an array
// member, positional continuation inside it, in-place refinement, an array of structs,
// and a chain through a union member.
TEST_F(CodegenTest, DesignatorChains)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int a[3]; int b; };
        struct e { char *name; int v; };
        struct p { int x, y; };
        union u { int i; struct p p; };
        struct w { union u u; int z; };
        struct s g1 = { .a[1] = 5, .b = 2 };
        struct s g2 = { .a[1] = 5, 6, 7 };
        struct s g3 = { .a = { 1, 2, 3 }, .a[1] = 9 };
        struct e gt[2] = { [1].v = 2, [0] = { "AB", 1 } };
        struct w gw = { .u.p.y = 2, 3 };
        void program() {
            struct s x1 = { .a[1] = 5, .b = 2 };
            struct s x2 = { .a[1] = 5, 6, 7 };
            struct s x3 = { .a = { 1, 2, 3 }, .a[1] = 9 };
            struct e xt[2] = { [1].v = 2, [0] = { "CD", 1 } };
            struct w xw = { .u.p.y = 2, 3 };
            printf("%d%d%d%d %d%d%d%d %d%d%d%d\n", g1.a[0], g1.a[1], g1.a[2], g1.b,
                   g2.a[0], g2.a[1], g2.a[2], g2.b, g3.a[0], g3.a[1], g3.a[2], g3.b);
            printf("%d%d%d%d %d%d%d%d %d%d%d%d\n", x1.a[0], x1.a[1], x1.a[2], x1.b,
                   x2.a[0], x2.a[1], x2.a[2], x2.b, x3.a[0], x3.a[1], x3.a[2], x3.b);
            printf("%s %d %d %d\n", gt[0].name, gt[0].v, gt[1].name == 0, gt[1].v);
            printf("%s %d %d %d\n", xt[0].name, xt[0].v, xt[1].name == 0, xt[1].v);
            printf("%d %d %d %d %d %d\n", gw.u.p.x, gw.u.p.y, gw.z, xw.u.p.x, xw.u.p.y, xw.z);
        }
    )");
    EXPECT_EQ("0502 0567 1930\n0502 0567 1930\nAB 1 1 2\nCD 1 1 2\n0 2 3 0 2 3\n", result);
}

// Compound literals with designators (k7), including union and array literals.  A
// multi-word struct literal gets its own frame slot: it used to get a single word, and
// its value was read from the slot holding its address, so neighbouring locals and the
// copied value were both garbage.
TEST_F(CodegenTest, CompoundLiteralDesignators)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int a, b, c; };
        union u { int i; char *p; };
        static int sum(struct s v) { return v.a + v.b + v.c; }
        void program() {
            struct s x;
            union u y;
            int k = 7, m = 8;
            int *q;
            x = (struct s){ .b = 2, .c = 3 };
            y = (union u){ .p = "AB" };
            q = (int[]){ [2] = 5, [0] = 4 };
            printf("%d %d %d %d %d\n", x.a, x.b, x.c, k, m);
            printf("%s %d %d %d %d\n", y.p, (union u){ 9 }.i, q[0], q[1], q[2]);
            printf("%d %d\n", sum((struct s){ .c = 1, .a = 2 }), (struct s){ .b = 6 }.b);
        }
    )");
    EXPECT_EQ("0 2 3 7 8\nAB 9 4 0 5\n3 6\n", result);
}

// The first word of a compound literal's slot is only ever read through its address.
// The slot bears a temporary's name, so peephole rule #28 used to drop the store to it
// as a dead temporary store.
TEST_F(CodegenTest, CompoundLiteralFirstWord)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        union u { int i; char *p; };
        void program() {
            int *q = (int[]){ [2] = 5, [0] = 4 };
            int *r = (int[3]){ 4, 0, 5 };
            printf("%d %d %d %d %d\n", (union u){ 9 }.i, q[0], q[2], r[0], r[2]);
        }
    )");
    EXPECT_EQ("9 4 5 4 5\n", result);
}

// A compound literal is an lvalue: every one gets its own slot, a scalar included, so a
// store through its address survives.  A literal in a loop is one object (C11 6.5.2.5p16).
// Assigning to a struct literal evaluates it once.
TEST_F(CodegenTest, CompoundLiteralLvalue)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int a, b; };
        static int sum(struct s *p) { return p->a + p->b; }
        void program() {
            int *p = &(int){ 5 };
            struct s *q = &(struct s){ 1 };
            int *b = &(struct s){ 1, 9 }.b;
            char *c = &(char){ 'A' };
            int *ps[3];
            struct s w = { 3, 4 };
            struct s u = ((struct s){ 1, 2 } = w);
            q->b = 7;
            *c = 'B';
            for (int i = 0; i < 3; i++)
                ps[i] = &(int){ i };
            printf("%d %d %d %d %d %c\n", *p, q->a, q->b, *b, sum(&(struct s){ 3, 4 }), *c);
            printf("%d %d %d %d\n", ++(int){ 0 }, (int){ 5 }--, (int){ 3 } += 4, --(int){ 5 });
            printf("%d %d %d %d\n", ps[0] == ps[2], *ps[2], u.a, u.b);
        }
    )");
    EXPECT_EQ("5 1 7 9 7 B\n1 5 7 4\n1 2 3 4\n", result);
}

// A struct/union defined in a compound literal's type name, or in sizeof, is registered.
TEST_F(CodegenTest, StructDefinedInTypeName)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        void program() {
            int b = (struct { int a, b; char c; }){ 1, 2, 'C' }.b;
            struct p { int x, y; } *q = &(struct p){ 5, 6 };
            char c = (struct { int a; char c; }){ .c = 'D' }.c;
            printf("%d %d %d %c %d %d\n", b, q->y, (int)sizeof(struct r { int a[3]; }),
                   c, ((union { int i; char *s; }){ 7 }).i, (struct p){ 8 }.x);
        }
    )");
    EXPECT_EQ("2 6 18 D 7 8\n", result);
}

// File-scope compound literals are anonymous static objects, writable through the pointer.
TEST_F(CodegenTest, FileScopeCompoundLiteral)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct s { int a; int b; char *n; };
        struct h { int *v; struct s *s; };
        int *p = (int[]){ 1, 2, 3 };
        int *p1 = (int[]){ 4, 5, 6 } + 1;
        struct s *q = &(struct s){ 7, 8, "AB" };
        int *qb = &(struct s){ 9, 10 }.b;
        struct h hh = { (int[]){ 11, 12 }, &(struct s){ .b = 13 } };
        char *c = &(char){ 'C' };
        char *cs = (char[]){ "DE" };
        int **pp = (int *[]){ (int[]){ 14 }, 0 };
        void program() {
            q->a += 100;
            p[0] = 15;
            printf("%d %d %d %d %d %s\n", p[2], p1[0], q->a, q->b, *qb, q->n);
            printf("%d %d %d %d %c %s %d\n", hh.v[1], hh.s->b, hh.s->a, p[0], *c, cs, pp[0][0]);
        }
    )");
    EXPECT_EQ("3 5 107 8 10 AB\n12 13 0 15 C DE 14\n", result);
}

// Bulk zero fill: an automatic aggregate with many zero leaves is zeroed by a loop, then
// only its non-zero leaves are stored.  The stack is dirtied first, so a missed zero shows.
TEST_F(CodegenTest, BulkZeroFill)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct m { char c; int x; char d[5]; int y[10]; double z; char *p; };
        struct n { struct m m[2]; int k; };
        int f(void) { return 42; }
        static void dirty(void) {
            int junk[200];
            for (int i = 0; i < 200; i++)
                junk[i] = -1;
            printf("%d", junk[3] & 0);
        }
        static int sum(const int *a, int n) {
            int s = 0;
            for (int i = 0; i < n; i++)
                s += a[i];
            return s;
        }
        static void test(void) {
            int a[100] = { [99] = 1, [50] = f() };
            struct m s = { 'A', .y[9] = 3, .d = { 0, 'B' } };
            struct n t = { .m[1].y[2] = 5, .k = 6 };
            int *q = (int[40]){ [0] = 7 };
            printf(" %d %d %d %d\n", a[99], a[50], sum(a, 100), a[0]);
            printf("%c %d %d %d %d %d %d\n", s.c, s.x, s.d[0], s.d[1], sum(s.y, 10), (int)s.z,
                   s.p != 0);
            printf("%d %d %d %d\n", sum(t.m[0].y, 10) + t.m[0].x + t.m[1].x, t.m[1].y[2], t.k,
                   t.m[0].c + t.m[1].d[4]);
            printf("%d\n", sum(q, 40));
        }
        void program() { dirty(); test(); }
    )");
    EXPECT_EQ("0 1 42 43 0\nA 0 0 66 3 0 0\n0 5 6 0\n7\n", result);
}

// Char arrays initialized from strings, zeroed by the loop when the zero tail is long.
// Also a braced string compound literal, which used to crash the translator.
TEST_F(CodegenTest, BulkZeroFillString)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <string.h>
        struct r { int id; char name[40]; int k; };
        static void dirty(void) {
            int junk[300];
            for (int i = 0; i < 300; i++)
                junk[i] = -1;
            printf("%d", junk[3] & 0);
        }
        static int zeros(const char *p, int n) {
            int z = 0;
            for (int i = 0; i < n; i++)
                z += p[i] == 0;
            return z;
        }
        static void test(void) {
            char a[60] = "AB";
            char e[4] = "CDE";
            char m[4][20] = { "FG", "H" };
            struct r s = { 5, "IJ", 7 };
            char *c = (char[30]){ "KL" };
            char x[12] = "M\0N";
            printf(" %s %d %s %s %s %d %d\n", a, zeros(a, 60), e, m[0], m[1], zeros(m[0], 80),
                   (int)strlen(m[3]));
            printf("%d %s %d %d %s %d %c %d\n", s.id, s.name, s.k, zeros(s.name, 40), c,
                   zeros(c, 30), x[2], zeros(x, 12));
        }
        void program() { dirty(); test(); }
    )");
    EXPECT_EQ("0 AB 58 CDE FG H 77 0\n5 IJ 7 38 KL 28 N 10\n", result);
}
