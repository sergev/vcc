//
// MSP430 structures: copies by words or bytes, arguments on the stack, results through
// the hidden pointer; and runs, the allocator included.
//
#include "msp430_test.h"

// The frontend copies a structure chunk by chunk, each a load and a store through the
// pointers: words for a 2-aligned one, bytes for a char-only one; memory to memory,
// each offset in the addresses.
EXPECT_CODE(StructCopyChunks, R"(mov @r13, 0(r12)
mov 2(r13), 2(r12)
mov 4(r13), 4(r12)
ret
)",
            "struct P { int x; long y; }; void f(struct P *p, struct P *q) { *p = *q; }")

EXPECT_CODE(StructCopyBytes, R"(mov.b @r13, 0(r12)
mov.b 1(r13), 1(r12)
mov.b 2(r13), 2(r12)
ret
)",
            "struct C { char c[3]; }; void f(struct C *p, struct C *q) { *p = *q; }")

// The callee copies a structure parameter into its slot on entry, from the address that
// came in r12 (kept meanwhile in the slot's first word): a large one through a counted
// loop from r15, memory to memory, r13 and r14 pushed around it.
TEST_F(Msp430Test, StructParamCopyLoop)
{
    std::string code = Code(CompileToMsp430(R"(
        struct B { int a[40]; };
        int g(struct B b) { return b.a[39]; }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r12, 0(r1)
mov r12, r15
push r14
push r13
mov r1, r14
add #4, r14
mov #40, r13
mov @r15, 0(r14)
incd r15
incd r14
dec r13
jne .Lv1
pop r13
pop r14
)")) << code;
}

// A structure argument past the registers goes as its address on the stack; the callee
// takes it from there.
TEST_F(Msp430Test, StructArgAddressOnStack)
{
    std::string code = Code(CompileToMsp430(R"(
        struct S { int a, b; };
        int w(int, int, int, int, struct S);
        int h(struct S *p) { return w(1, 2, 3, 4, *p); }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r1, r15
incd r15
mov r15, 0(r1)
mov #1, r12
)")) << code;
}

// A structure the callee only reads, with no call and no store that could change the
// caller's object meanwhile, is read through the pointer that came, uncopied.
EXPECT_CODE(StructParamFromStack, R"(mov 2(r1), r15
mov 2(r15), r12
ret
)",
            "struct S { int a, b; }; int w(int a, int b, int c, int d, struct S s) { return s.b; }")

// One the callee writes is copied into its slot first.
TEST_F(Msp430Test, StructParamWrittenCopied)
{
    std::string code = Code(CompileToMsp430(R"(
        struct S { int a, b; };
        int w(int a, int b, int c, int d, struct S s) { s.b += a; return s.b; }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(mov @r15, 0(r1)
mov 2(r15), 2(r1)
)")) << code;
}

// So is one read in a function that makes a call: the callee might change the object.
TEST_F(Msp430Test, StructParamCopiedAcrossCall)
{
    std::string code = Code(CompileToMsp430(R"(
        struct S { int a, b; };
        void g(void);
        int w(struct S s) { g(); return s.b; }
    )"));
    // Only the member read after the call is copied.
    EXPECT_NE(std::string::npos, code.find(R"(mov 2(r12), 2(r1)
call #g
mov 2(r1), r12
)")) << code;
}

// Read through the pointer that came in a register, a structure parameter needs no slot,
// and the function no frame.
EXPECT_CODE(StructParamInRegisterFrameless, R"(mov 2(r12), r12
ret
)",
            "struct S { int a, b, c; }; int f(struct S s) { return s.b; }")

// A structure parameter written and passed on: the copy of the member overwritten is
// dead, and so is the slot's first word, which held the incoming address.  The call
// reads r12 and r13 only, so nothing is moved into r15 for it.
EXPECT_CODE(StructParamDeadCopyWords, R"(sub #6, r1
mov 2(r12), 2(r1)
mov 4(r12), 4(r1)
mov r13, 0(r1)
mov r1, r12
call #g
add #6, r1
ret
)",
            R"(
    struct S { int a, b, c; };
    int g(struct S s);
    int f(struct S s, int k) { s.a = k; return g(s); }
)")

// Read through the pointer or copied, a structure parameter keeps its value, and the
// caller's object stays as it was.
TEST_F(Msp430Test, RunStructParamByReference)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        struct S { int a, b; long c; char d[3]; };
        struct S glob = { 1, 2, 3L, "xy" };
        static int sum(struct S s) { return s.a + s.b + (int)s.c + s.d[0] + s.d[1]; }
        static struct S id(struct S s) { return s; }
        static int changed(struct S s) { s.a = 100; return s.a + s.b; }
        static int across(struct S s) { glob.a = 50; return s.a; }
        int main(void)
        {
            struct S x = { 10, 20, 30L, "AB" };
            if (sum(x) != 10 + 20 + 30 + 'A' + 'B')
                return 1;
            struct S y = id(x);
            if (y.a != 10 || y.c != 30L || y.d[1] != 'B')
                return 2;
            if (changed(x) != 120 || x.a != 10)
                return 3;
            if (across(glob) != 1 || glob.a != 50)
                return 4;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// The callee hands the hidden pointer back in r12, where it came.  The local is copied
// from its register, so its slot is never read, and the store to it goes.
EXPECT_CODE(StructResultPointerReturned, R"(mov r13, 0(r12)
ret
)",
            "struct S { int a; }; struct S f(int x) { struct S s = { x }; return s; }")

// Structures of every size passed and returned, a union, nested members, arrays of
// structures and their copies.
TEST_F(Msp430Test, RunStructs)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        struct S1 { char c; };
        struct S3 { char c[3]; };
        struct S4 { int a; int b; };
        struct S10 { char c; long l; double d; };
        struct Big { int a[30]; };
        union U { long l; char c[4]; };
        struct Outer { int k; struct S4 in; char tag; };
        struct S1 r1(struct S1 s) { s.c++; return s; }
        struct S3 r3(struct S3 s) { s.c[2] = 'z'; return s; }
        struct S4 r4(int x, struct S4 s, int y) { s.a += x; s.b += y; return s; }
        struct S10 r10(struct S10 s) { s.l *= 2; s.c = 'q'; return s; }
        struct Big rb(struct Big b) { for (int i = 0; i < 30; i++) b.a[i] *= 2; return b; }
        union U ru(long l) { union U u; u.l = l; return u; }
        int main(void)
        {
            struct S1 a = { 'a' };
            if (r1(a).c != 'b' || a.c != 'a') return 1;
            struct S3 t = { { 'x', 'y', 'w' } };
            struct S3 t2 = r3(t);
            if (t2.c[0] != 'x' || t2.c[2] != 'z' || t.c[2] != 'w') return 2;
            struct S4 p = { 10, 20 };
            p = r4(1, p, 2);
            if (p.a != 11 || p.b != 22) return 3;
            struct S10 q = { 'c', 70000L, 0.0 };
            q = r10(q);
            if (q.l != 140000L || q.c != 'q') return 4;
            struct Big b;
            for (int i = 0; i < 30; i++) b.a[i] = i;
            struct Big b2 = rb(b);
            if (b2.a[29] != 58 || b.a[29] != 29) return 5;
            union U u = ru(0x41424344L);
            if (u.c[0] != 0x44 || u.c[3] != 0x41) return 6;
            struct Outer o = { 1, { 2, 3 }, 'T' };
            struct Outer arr[3];
            for (int i = 0; i < 3; i++) { arr[i] = o; arr[i].in.b += i; }
            if (arr[2].in.b != 5 || arr[1].tag != 'T') return 7;
            struct Outer *op = &arr[1];
            if (op->in.a != 2 || (op + 1)->in.b != 5) return 8;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// malloc, realloc (which copies) and calloc (which clears, and refuses an overflowing
// size); a request the heap cannot meet below the stack is refused.
TEST_F(Msp430Test, RunMalloc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("abcdefghij 0 overflow exhausted", CompileAndRunMsp430(R"(
        #include <stdlib.h>
        void putbyte(int c);
        static void puts_(const char *s) { while (*s) putbyte(*s++); }
        int main(void)
        {
            char *m = malloc(10);
            for (int i = 0; i < 10; i++)
                m[i] = 'a' + i;
            char *r = realloc(m, 20);
            r[10] = 0;
            puts_(r);
            putbyte(' ');
            char *c = calloc(5, 3);
            int sum = 0;
            for (int i = 0; i < 15; i++)
                sum += c[i];
            putbyte('0' + sum);
            puts_(calloc(300, 300) == 0 ? " overflow" : " no overflow");
            puts_(malloc(16000) == 0 ? " exhausted" : " not exhausted");
            free(r);
            return 0;
        }
    )"));
}

// Structures by reference across the two compilers: GCC's callee and ours each write to
// the parameter, and each caller's object stays as it was; results come back through
// the hidden pointer both ways.
TEST_F(Msp430Test, RunStructsWithGcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string ours = CompileToMsp430(R"(
        struct S3 { char c[3]; };
        struct S4 { int a, b; };
        struct S10 { char c; long l; double d; };
        int gcc_take(int x, struct S4 s, int y);
        struct S4 gcc_ret(struct S4 s);
        int gcc_calls_ours(void);
        int our_take(struct S3 s, long l, struct S10 t)
        {
            s.c[0] = 'q';
            t.l++;
            return s.c[0] + s.c[1] + (int)l + (int)(t.l - 70000L) + t.c;
        }
        struct S4 our_ret(int k, struct S4 s)
        {
            s.a *= k;
            return s;
        }
        int main(void)
        {
            struct S4 a = { 1, 2 };
            if (gcc_take(10, a, 20) != 133)
                return 1;
            if (a.a != 1 || a.b != 2)
                return 2;
            struct S4 b = gcc_ret(a);
            if (b.a != 1 || b.b != 6 || a.b != 2)
                return 3;
            int r = gcc_calls_ours();
            return r == 'q' + 'y' + 5 + 1 + 'c' ? 0 : 4;
        }
    )");
    std::string gcc  = R"(
        struct S3 { char c[3]; };
        struct S4 { int a, b; };
        struct S10 { char c; long l; double d; };
        int our_take(struct S3 s, long l, struct S10 t);
        struct S4 our_ret(int k, struct S4 s);
        int gcc_take(int x, struct S4 s, int y)
        {
            s.a += 100;
            return s.a + s.b + x + y;
        }
        struct S4 gcc_ret(struct S4 s)
        {
            s.b *= 3;
            return s;
        }
        int gcc_calls_ours(void)
        {
            struct S3 s = { { 'x', 'y', 'z' } };
            struct S10 t = { 'c', 70000L, 1.5 };
            int r = our_take(s, 5L, t);
            if (s.c[0] != 'x' || t.l != 70000L || t.d != 1.5)
                return -1;
            struct S4 u = { 7, 8 };
            struct S4 v = our_ret(3, u);
            if (v.a != 21 || v.b != 8 || u.a != 7)
                return -2;
            return r;
        }
    )";
    EXPECT_EQ("", GccRun(gcc, ours));
    EXPECT_EQ(0, exit_status);
}
