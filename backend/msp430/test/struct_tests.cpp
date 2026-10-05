//
// MSP430 structures: copies by words or bytes, arguments on the stack, results through
// the hidden pointer; and runs, the allocator included.
//
#include "msp430_test.h"

// The frontend copies a structure chunk by chunk, each a load and a store through the
// pointers: words for a 2-aligned one, bytes for a char-only one.
TEST_F(Msp430Test, StructCopyChunks)
{
    std::string code = Code(CompileToMsp430(R"(
        struct P { int x; long y; };
        void f(struct P *p, struct P *q) { *p = *q; }
    )"));
    EXPECT_NE(std::string::npos, code.find("mov @r15, ")) << code;
    EXPECT_EQ(std::string::npos, code.find("mov.b")) << code;
}

TEST_F(Msp430Test, StructCopyBytes)
{
    std::string code = Code(CompileToMsp430(
        "struct C { char c[3]; }; void f(struct C *p, struct C *q) { *p = *q; }"));
    EXPECT_NE(std::string::npos, code.find("mov.b @r15, ")) << code;
}

// A large structure argument goes to the stack through a counted loop; @r14+ only into
// a register (clang's assembler).
TEST_F(Msp430Test, StructArgCopyLoop)
{
    std::string code = Code(CompileToMsp430(R"(
        struct B { int a[40]; };
        int g(struct B b);
        int f(struct B *p) { return g(*p); }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r1, r15
mov #40, r13
)")) << code;
    EXPECT_NE(std::string::npos,
              code.find(R"(mov @r14+, r12
mov r12, 0(r15)
incd r15
dec r13
jne )"))
        << code;
}

// A structure argument is copied into the outgoing area.
TEST_F(Msp430Test, StructArgOnStack)
{
    std::string code = Code(CompileToMsp430(R"(
        struct S { int a, b; };
        int g(int x, struct S s);
        int f(struct S *p) { return g(1, *p); }
    )"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r1, r15
mov @r14, 0(r15)
mov 2(r14), 2(r15)
)"))
        << code;
}

// The callee hands the hidden pointer back in r12.
TEST_F(Msp430Test, StructResultPointerReturned)
{
    std::string code = Code(CompileToMsp430(
        "struct S { int a; }; struct S f(int x) { struct S s = { x }; return s; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov 0(r1), r12
add #)")) << code;
}

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
