//
// wasm32 structures and unions: as clang's wasm32 passes them.  One holding a single
// scalar travels as it, an empty one not at all, any other as the address of a copy
// the caller makes; a result that is no wasm value goes where a hidden first parameter
// points.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// Signatures: the single-scalar rule through nesting, one-element arrays, a union and a
// lone bit-field (the integer of the structure's size); by reference otherwise.
TEST_F(WasmTest, StructSignatures)
{
    std::string s = CompileToWasm(R"(
        struct I { int x; };
        struct N { struct { double d; } in; };
        struct A1 { float a[1]; };
        union U1 { long long x; };
        struct B { long long a : 3; };
        struct B2 { int a : 3, b : 4; };
        struct Two { int a, b; };
        struct C3 { char c[3]; };
        union U2 { int x; float f; };
        struct LD { long double d; };
        struct E { int z[0]; };
        void fI(struct I a);
        void fN(struct N a);
        void fA1(struct A1 a);
        void fU1(union U1 a);
        void fB(struct B a);
        void fB2(struct B2 a);
        void fTwo(struct Two a);
        void fC3(struct C3 a);
        void fU2(union U2 a);
        void fLD(struct LD a, long double b);
        void fE(int x, struct E e, int y);
        struct I rI(void);
        struct Two rTwo(int k);
        struct LD rLD(void);
        long double rld(void);
        struct E rE(void);
        int use(void)
        {
            return (int)(long)fI + (int)(long)fN + (int)(long)fA1 + (int)(long)fU1 +
                   (int)(long)fB + (int)(long)fB2 + (int)(long)fTwo + (int)(long)fC3 +
                   (int)(long)fU2 + (int)(long)fLD + (int)(long)fE + (int)(long)rI +
                   (int)(long)rTwo + (int)(long)rLD + (int)(long)rld + (int)(long)rE;
        }
    )");
    for (const char *want : {
             "fI (i32) -> ()", "fN (f64) -> ()", "fA1 (f32) -> ()", "fU1 (i64) -> ()",
             "fB (i64) -> ()", "fB2 (i32) -> ()", "fTwo (i32) -> ()", "fC3 (i32) -> ()",
             "fU2 (i32) -> ()", "fLD (i64, i64, i64, i64) -> ()", "fE (i32, i32) -> ()",
             "rI () -> (i32)", "rTwo (i32, i32) -> ()", "rLD (i32) -> ()", "rld (i32) -> ()",
             "rE () -> ()" })
        EXPECT_NE(s.find(std::string("\t.functype\t") + want + "\n"), std::string::npos)
            << want << "\n"
            << s;
}

// A structure of one scalar is loaded from its object for the call, and its result
// stored into one.
EXPECT_CODE(SingleScalarArgument,
            "global.get __stack_pointer\ni32.const 16\ni32.sub\nlocal.tee 3\n"
            "global.set __stack_pointer\nlocal.get 3\ni32.const 7\ni32.store 0\n"
            "local.get 3\nlocal.get 3\ni32.load 0\ncall g\ni32.store 4\nlocal.get 3\n"
            "i32.const 4\ni32.add\nlocal.set 0\nlocal.get 0\nlocal.set 1\nlocal.get 1\n"
            "i32.load 0\nlocal.set 2\nlocal.get 2\nlocal.get 3\ni32.const 16\ni32.add\n"
            "global.set __stack_pointer\nreturn\nend_function\n",
            "struct I { int x; }; struct I g(struct I); int f(void)"
            "{ struct I a; a.x = 7; return g(a).x; }")

// A structure passed by reference is copied into the calls' area, and its address
// passed; the callee uses its parameter's object in place.
TEST_F(WasmTest, ByReferenceArgument)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm(R"(
        struct Two { int a, b; };
        int g(struct Two);
        int f(struct Two t) { t.b = 5; return g(t) + t.a; }
    )"));
    EXPECT_NE(s.find("local.get 0\ni32.const 5\ni32.store 4\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i32.const 8\nmemory.copy 0, 0\n"), std::string::npos) << s;
}

// The result through the hidden parameter: in place when its destination is a slot no
// pointer reaches.
TEST_F(WasmTest, ResultThroughMemory)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm(R"(
        struct Two { int a, b; };
        struct Two g(int k);
        struct Two f(int k) { struct Two t = g(k); t.a++; return t; }
    )"));
    EXPECT_NE(s.find("call g\n"), std::string::npos) << s;
    // The result's copy to where f's own hidden parameter points.
    EXPECT_NE(s.find("local.get 0\n"), std::string::npos) << s;
    EXPECT_EQ(s.find("i32.load 0\nreturn\n"), std::string::npos) << s;
}

// Run: structures of every kind as arguments and results, a callee changing its copy,
// x = f(x), unions, a long double through two i64 and a hidden result, and arrays of
// structures.
TEST_F(WasmTest, RunStructCalls)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        struct I { int x; };
        struct D { struct { double d; } in; };
        struct Two { int a, b; };
        struct C3 { char c[3]; };
        struct Big { long long a; char s[20]; double d; };
        union U { int i; char c[4]; };
        struct LD { long double d; };
        struct E { int z[0]; };

        static struct I incI(struct I a) { a.x++; return a; }
        static struct D twice(struct D a) { a.in.d *= 2; return a; }
        static struct Two swap(struct Two t) { int k = t.a; t.a = t.b; t.b = k; return t; }
        static struct C3 rot(struct C3 c) { char k = c.c[0]; c.c[0] = c.c[1]; c.c[1] = c.c[2]; c.c[2] = k; return c; }
        static struct Big grow(struct Big b, int k) { b.a += k; b.s[19] = 'z'; b.d += 0.5; return b; }
        static union U bump(union U u) { u.c[0]++; return u; }
        static struct LD ld(struct LD x, long double y) { struct LD r = x; (void)y; return r; }
        static long double ldv(long double y) { return y; }
        static int empty(int a, struct E e, int b) { (void)e; return a - b; }
        static struct E none(void) { struct E e; return e; }

        int main(void)
        {
            struct I i = { 41 };
            if (incI(i).x != 42 || i.x != 41)
                return 1;
            struct D d = { { 1.5 } };
            if (twice(d).in.d != 3.0)
                return 2;
            struct Two t = { 1, 2 };
            t = swap(t);
            if (t.a != 2 || t.b != 1)
                return 3;
            struct C3 c = { { 'a', 'b', 'c' } };
            c = rot(c);
            if (c.c[0] != 'b' || c.c[2] != 'a')
                return 4;
            struct Big b = { 1, "x", 1.0 };
            struct Big b2 = grow(b, 9);
            if (b2.a != 10 || b2.s[19] != 'z' || b2.d != 1.5 || b.s[19] != 0 || b.a != 1)
                return 5;
            union U u;
            u.i = 0;
            if (bump(bump(u)).c[0] != 2 || u.c[0] != 0)
                return 6;
            struct LD l = { 0 }, m = ld(l, l.d);
            (void)ldv(m.d);
            if (empty(7, none(), 3) != 4)
                return 7;
            struct Two arr[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
            arr[1] = swap(arr[2]);
            if (arr[1].a != 6 || arr[1].b != 5 || arr[2].a != 5)
                return 8;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: function pointers, as table indices: called, passed, returned, in an array of
// static data, and compared.
TEST_F(WasmTest, RunFunctionPointers)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        struct Two { int a, b; };
        static int add(int a, int b) { return a + b; }
        static int sub(int a, int b) { return a - b; }
        static struct Two pair(int a) { struct Two t = { a, -a }; return t; }
        static int (*ops[])(int, int) = { add, sub };
        typedef int (*op)(int, int);
        static op pick(int k) { return k ? sub : add; }
        static int apply(op f, int a, int b) { return f(a, b); }
        int main(void)
        {
            struct Two (*pf)(int) = pair;
            if (ops[0](3, 4) != 7 || ops[1](3, 4) != -1)
                return 1;
            if (pick(1)(10, 3) != 7 || apply(pick(0), 10, 3) != 13)
                return 2;
            if (pf(5).b != -5)
                return 3;
            if (ops[0] != add || pick(1) != sub)
                return 4;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
