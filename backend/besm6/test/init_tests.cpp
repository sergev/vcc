#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "codegen_test.h"

// A block-scope static is emitted as a module-local labeled datum inside its owning
// function's own ,name,/,end, module, after the code (before ,end,) — not as a separate
// top-level module, and without an external SUBP declaration.
TEST_F(CodegenTest, StaticLocalEmittedInsideFunctionModule)
{
    std::string m = CompileToMadlen("int foo(void) { static int x; x = x + 1; return x; }");

    // Exactly one module: a single ,name, for foo (no separate module for x).
    EXPECT_EQ(std::string::npos, m.find(",name,", m.find(",name,") + 1));

    // The storage is a labeled zero word placed after the code (the ,uj, b/ret) and before
    // the closing ,end,.  Inside a code module the storage must be an explicit ,log, 0
    // (not a ,bss, reservation) — the loader does not zero ,bss, space spliced into a code
    // module, so a zero-init static local would otherwise read back garbage.
    size_t code  = m.find(",uj, b/ret");
    size_t datum = m.find("x:");
    size_t end   = m.rfind(",end,");
    ASSERT_NE(std::string::npos, code);
    ASSERT_NE(std::string::npos, datum);
    ASSERT_NE(std::string::npos, end);
    EXPECT_LT(code, datum);
    EXPECT_LT(datum, end);
    EXPECT_NE(std::string::npos, m.find("x:   ,log, 0"));

    // No external declaration for the in-module label.
    EXPECT_EQ(std::string::npos, m.find("x:   ,subp,"));
}

// An initialized static local emits its constant value as the labeled datum.
TEST_F(CodegenTest, StaticLocalInitializedValue)
{
    std::string m = CompileToMadlen("int bar(void) { static int x = 4; return x; }");
    EXPECT_NE(std::string::npos, m.find("x:   ,log, 4"));
}

// Same-named statics in two functions stay distinct: the first keeps the plain `x:` label,
// a later same-named static gets a `$N` suffix (`x$1` -> Madlen `x/1`).  This uniqueness is
// what lets the flat Unix (b6as) object — which, unlike Madlen, has no per-function module
// scoping — avoid a duplicate-symbol collision.
TEST_F(CodegenTest, StaticLocalSameNameDistinctFunctions)
{
    std::string m = CompileToMadlen(
        "int foo(void) { static int x = 1; return x; }\n"
        "int bar(void) { static int x = 2; return x; }");
    EXPECT_NE(std::string::npos, m.find("x:   ,log, 1"));
    EXPECT_NE(std::string::npos, m.find("x/1:   ,log, 2"));
}

TEST_F(CodegenTest, VarIntTentative)
{
    std::string output = CompileToMadlen("int foo;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,bss, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarIntPtrTentative)
{
    std::string output = CompileToMadlen("int *foo;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,bss, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarCharPtrTentative)
{
    std::string output = CompileToMadlen("char *foo;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,bss, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarVoidPtrTentative)
{
    std::string output = CompileToMadlen("void *foo;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,bss, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarIntArrayTentative)
{
    std::string output = CompileToMadlen("int arr[5];");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,bss, 5
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarCharArrayTentative)
{
    std::string output = CompileToMadlen("char arr[10];");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,bss, 2
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarPtrArrayTentative)
{
    std::string output = CompileToMadlen("int *arr[4];");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,bss, 4
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarDoubleArrayTentative)
{
    std::string output = CompileToMadlen("double arr[3];");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,bss, 3
             ,end,
)",
              output);
}

TEST_F(CodegenTest, Var2DArrayTentative)
{
    std::string output = CompileToMadlen("int arr[2][3];");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,bss, 6
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarStruct1FieldTentative)
{
    std::string output = CompileToMadlen("struct { int x; } s;");
    EXPECT_EQ(R"(c
        s:   ,name,
             ,bss, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarStruct2FieldTentative)
{
    std::string output = CompileToMadlen("struct { int x; int y; } s;");
    EXPECT_EQ(R"(c
        s:   ,name,
             ,bss, 2
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarStruct3FieldTentative)
{
    std::string output = CompileToMadlen("struct { int x; int y; int z; } s;");
    EXPECT_EQ(R"(c
        s:   ,name,
             ,bss, 3
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarStructMixedTentative)
{
    std::string output = CompileToMadlen("struct { char c; int n; double d; } s;");
    EXPECT_EQ(R"(c
        s:   ,name,
             ,bss, 3
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarNamedStructTentative)
{
    std::string output = CompileToMadlen(R"(
        struct pt {
            int x;
            int y;
        };
        struct pt s;
)");
    EXPECT_EQ(R"(c
        s:   ,name,
             ,bss, 2
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarIntInit)
{
    std::string output = CompileToMadlen("int foo = 42;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 52
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarLongInit)
{
    std::string output = CompileToMadlen("long foo = 4321;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 10341
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarShortInit)
{
    std::string output = CompileToMadlen("short foo = 123;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 173
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarCharInit)
{
    std::string output = CompileToMadlen("char foo = '+';");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 53
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarUnsignedInit)
{
    std::string output = CompileToMadlen("unsigned foo = 01234567076543210;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 1234567076543210
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarIntPtrInitName)
{
    std::string output = CompileToMadlen("extern int foo; int *bar = &foo;");
    EXPECT_EQ(R"(c
      bar:   ,name,
      foo:   ,subp,
             ,z00,
             ,z00, foo
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarIntPtrInitLiteral)
{
    std::string output = CompileToMadlen("int *bar = (int*) 42;");
    EXPECT_EQ(R"(c
      bar:   ,name,
             ,log, 52
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarCharPtrInit)
{
    std::string output = CompileToMadlen("extern char foo; char *bar = &foo;");
    EXPECT_EQ(R"(c
      bar:   ,name,
      foo:   ,subp,
           8 ,z00,
             ,z00, foo
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarVoidPtrInitChar)
{
    std::string output = CompileToMadlen("extern char foo; void *bar = &foo;");
    EXPECT_EQ(R"(c
      bar:   ,name,
      foo:   ,subp,
           8 ,z00,
             ,z00, foo
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarVoidPtrInitInt)
{
    std::string output = CompileToMadlen("extern int foo; void *bar = &foo;");
    EXPECT_EQ(R"(c
      bar:   ,name,
      foo:   ,subp,
          13 ,z00,
             ,z00, foo
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarIntPtrInitNameOffset)
{
    std::string output = CompileToMadlen("extern int foo[]; int *bar = &foo[5];");
    EXPECT_EQ(R"(c
      bar:   ,name,
      foo:   ,subp,
             ,z00,
             ,z00, foo+5
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarCharPtrInitNameOffset)
{
    std::string output = CompileToMadlen("extern char foo[]; char *bar = &foo[15];");
    EXPECT_EQ(R"(c
      bar:   ,name,
      foo:   ,subp,
          10 ,z00,
             ,z00, foo+2
             ,end,
)",
              output);
}

// A static pointer initializer may be any C11 §6.6 address constant: an array/function name,
// &lvalue, and constant pointer arithmetic, composed in any order.  build_static_init folds the
// whole expression to a base symbol + word offset (backend/besm6/static.c renders name+word).

// Array name plus a constant: `arr + 2` — decays to &arr[0], then +2 elements.
TEST_F(CodegenTest, VarIntPtrInitArrayPlusConst)
{
    std::string output = CompileToMadlen("extern int arr[]; int *p = arr + 2;");
    EXPECT_EQ(R"(c
        p:   ,name,
      arr:   ,subp,
             ,z00,
             ,z00, arr+2
             ,end,
)",
              output);
}

// Address of an element with further arithmetic: `&arr[1] + 1` == arr+2.
TEST_F(CodegenTest, VarIntPtrInitElemPlusConst)
{
    std::string output = CompileToMadlen("extern int arr[]; int *p = &arr[1] + 1;");
    EXPECT_EQ(R"(c
        p:   ,name,
      arr:   ,subp,
             ,z00,
             ,z00, arr+2
             ,end,
)",
              output);
}

// Element of an array member: `&s.v[2]` — a(word0), v(word1), v[2] -> word3.
TEST_F(CodegenTest, VarIntPtrInitArrayMemberElem)
{
    std::string output =
        CompileToMadlen("struct S{int a; int v[4];}; extern struct S s; int *p = &s.v[2];");
    EXPECT_EQ(R"(c
        p:   ,name,
        s:   ,subp,
             ,z00,
             ,z00, s+3
             ,end,
)",
              output);
}

// Nested member chain: `&o.in.y` — a(word0), in(word1), in.y -> word2.
TEST_F(CodegenTest, VarIntPtrInitNestedMember)
{
    std::string output = CompileToMadlen(
        "struct I{int x,y;}; struct O{int a; struct I in;}; "
        "extern struct O o; int *p = &o.in.y;");
    EXPECT_EQ(R"(c
        p:   ,name,
        o:   ,subp,
             ,z00,
             ,z00, o+2
             ,end,
)",
              output);
}

// Member of an array element: `&arr[1].b` — arr[1](word2), .b -> word3.
TEST_F(CodegenTest, VarIntPtrInitElementMember)
{
    std::string output =
        CompileToMadlen("struct S{int a,b;}; extern struct S arr[]; int *p = &arr[1].b;");
    EXPECT_EQ(R"(c
        p:   ,name,
      arr:   ,subp,
             ,z00,
             ,z00, arr+3
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarFloatInit)
{
    std::string output = CompileToMadlen("float foo = 3.1415;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, 3.1415
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarDoubleInit)
{
    std::string output = CompileToMadlen("double foo = 2.71828e-25;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, 2.71828e-25
             ,end,
)",
              output);
}

// A real initializer that is not a bare literal: unary minus, binary arithmetic, a
// comparison, a logical NOT and a cast all fold in the frontend before emission.
TEST_F(CodegenTest, VarDoubleNegInit)
{
    std::string output = CompileToMadlen("double foo = -0.5;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, -0.5
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarFloatNegInit)
{
    std::string output = CompileToMadlen("float foo = -3.1415;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, -3.1415
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarDoubleFromNegIntInit)
{
    std::string output = CompileToMadlen("double foo = -1;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, -1.
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarDoubleConstExprInit)
{
    std::string output = CompileToMadlen("double foo = 1.0 / 4.0;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, 0.25
             ,end,
)",
              output);
}

// A comparison and a logical NOT yield an int, which converts to the real target.
TEST_F(CodegenTest, VarDoubleCompareInit)
{
    std::string output = CompileToMadlen("double foo = (1.5 < 2.0);");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, 1.
             ,end,
)",
              output);
}

TEST_F(CodegenTest, VarDoubleLogNotInit)
{
    std::string output = CompileToMadlen("double foo = !0.0;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, 1.
             ,end,
)",
              output);
}

// A cast to a narrower type wraps to that type's width: (char)300 is 44 (octal 54).
TEST_F(CodegenTest, VarIntCastInit)
{
    std::string output = CompileToMadlen("int foo = (char)300;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 54
             ,end,
)",
              output);
}

// A real constant expression converts to an integer target by truncation toward zero.
TEST_F(CodegenTest, VarIntFromRealInit)
{
    std::string output = CompileToMadlen("int foo = (int)1.5;");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, ArrayDoubleNegInit)
{
    std::string output = CompileToMadlen("double foo[] = { -1.5, 2.5 };");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, -1.5
             ,real, 2.5
             ,end,
)",
              output);
}

TEST_F(CodegenTest, ArrayIntInit)
{
    std::string output = CompileToMadlen("int foo[] = { 12, 34, 56 };");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 14
             ,log, 42
             ,log, 70
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StructIntInit)
{
    std::string output = CompileToMadlen("struct { int foo, bar; } quz = { 12, 34 };");
    EXPECT_EQ(R"(c
      quz:   ,name,
             ,log, 14
             ,log, 42
             ,end,
)",
              output);
}

TEST_F(CodegenTest, ArrayDoubleInit)
{
    std::string output = CompileToMadlen("double foo[] = { 1.5, 2.5, 3.5 };");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,real, 1.5
             ,real, 2.5
             ,real, 3.5
             ,end,
)",
              output);
}

TEST_F(CodegenTest, Array2DIntInit)
{
    std::string output = CompileToMadlen("int foo[2][3] = { {1, 2, 3}, {4, 5, 6} };");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 1
             ,log, 2
             ,log, 3
             ,log, 4
             ,log, 5
             ,log, 6
             ,end,
)",
              output);
}

TEST_F(CodegenTest, ArrayStructInit)
{
    std::string output = CompileToMadlen(R"(
        struct pt {
            int x, y;
        };
        struct pt foo[] = { {1, 2}, {3, 4} };
)");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 1
             ,log, 2
             ,log, 3
             ,log, 4
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StructArrayInit)
{
    std::string output = CompileToMadlen(R"(
        struct {
            int arr[3];
        } foo = { {10, 20, 30} };
)");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 12
             ,log, 24
             ,log, 36
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StructMixedInit)
{
    std::string output = CompileToMadlen(R"(
        struct {
            int n;
            double d;
        } foo = { 7, 2.5 };
)");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 7
             ,real, 2.5
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StructNestedInit)
{
    std::string output = CompileToMadlen(R"(
        struct pt {
            int x, y;
        };
        struct {
            struct pt p;
            int z;
        } foo = { {1, 2}, 3 };
)");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 1
             ,log, 2
             ,log, 3
             ,end,
)",
              output);
}

TEST_F(CodegenTest, ArrayPartialInit)
{
    std::string output = CompileToMadlen("int foo[5] = { 1, 2 };");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 1
             ,log, 2
             ,bss, 3
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StructPartialInit)
{
    std::string output = CompileToMadlen(R"(
        struct {
            int a;
            int b;
            int c;
        } foo = { 5 };
)");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 5
             ,bss, 2
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrEmptyInit)
{
    std::string output = CompileToMadlen("char foo[] = \"\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 0
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrSingleCharInit)
{
    std::string output = CompileToMadlen("char foo[] = \"A\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2020000000000000
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrThreeCharsInit)
{
    std::string output = CompileToMadlen("char foo[] = \"ABC\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2024110300000000
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrSixCharsNoNullInit)
{
    std::string output = CompileToMadlen("char foo[6] = \"ABCDEF\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2024110321042506
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrSixCharsWithNullInit)
{
    std::string output = CompileToMadlen("char foo[] = \"ABCDEF\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2024110321042506
             ,log, 0
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrSevenCharsInit)
{
    std::string output = CompileToMadlen("char foo[] = \"ABCDEFG\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2024110321042506
             ,log, 2160000000000000
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrWithZeroPaddingInit)
{
    std::string output = CompileToMadlen("char foo[8] = \"ABC\";");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2024110300000000
             ,bss, 1
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrCyrillicInit)
{
    // "Абракадабра" — 11 Cyrillic chars + NUL = 12 bytes = 2 BESM-6 words.
    // UTF-8 → KOI7: А→41 б→62 р→50 а→41 к→4B а→41 | д→64 а→41 б→62 р→50 а→41 00
    std::string output = CompileToMadlen(R"(
        char foo[] = "Абракадабра";
)");
    EXPECT_EQ(R"(c
      foo:   ,name,
             ,log, 2026112020245501
             ,log, 3104054224040400
             ,end,
)",
              output);
}

// ---------------------------------------------------------------------------
// String pointer initialization — TAC_TOPLEVEL_STATIC_CONSTANT tests
// ---------------------------------------------------------------------------
// A `char *p = "..."` string constant (_str0) is folded into the module that
// references it (here p): no separate global ,name, module and no external ,subp,
// — the packed-char log words are appended as a module-local label, so the per-unit
// _strN name can no longer collide across separately assembled objects.

TEST_F(CodegenTest, StrConstantEmptyPtr)
{
    std::string output = CompileToMadlen("char *p = \"\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 0
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrConstantSingleCharPtr)
{
    std::string output = CompileToMadlen("char *p = \"A\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2020000000000000
             ,end,
)",
              output);
}

TEST_F(CodegenTest, StrConstantThreeCharsPtr)
{
    std::string output = CompileToMadlen("char *p = \"ABC\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2024110300000000
             ,end,
)",
              output);
}

// "ABCDE\0" = 6 bytes, exactly one packed word (no second word needed).
TEST_F(CodegenTest, StrConstantFiveCharsPtr)
{
    std::string output = CompileToMadlen("char *p = \"ABCDE\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2024110321042400
             ,end,
)",
              output);
}

// "ABCDEF\0" = 7 bytes → two words: full word + null-only word.
TEST_F(CodegenTest, StrConstantSixCharsPtr)
{
    std::string output = CompileToMadlen("char *p = \"ABCDEF\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2024110321042506
             ,log, 0
             ,end,
)",
              output);
}

// "ABCDEFG\0" = 8 bytes → two words: ABCDEF + G\0.
TEST_F(CodegenTest, StrConstantSevenCharsPtr)
{
    std::string output = CompileToMadlen("char *p = \"ABCDEFG\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2024110321042506
             ,log, 2160000000000000
             ,end,
)",
              output);
}

// Two declarations processed separately: each gets its own _strN constant, folded
// into its own variable's module.  symtab_add_string assigns unique names regardless
// of string content.
TEST_F(CodegenTest, StrConstantTwoPtrs)
{
    std::string output = CompileToMadlen("char *p = \"ABC\"; char *q = \"ABC\";");
    EXPECT_EQ(R"(c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2024110300000000
             ,end,
c
        q:   ,name,
          13 ,z00,
             ,z00, *str1
    *str1:   ,log, 2024110300000000
             ,end,
)",
              output);
}

// A char array init uses TAC_STATIC_INIT_STRING directly (no static constant).
// A char pointer init's _str0 constant is folded into the pointer variable's module.
TEST_F(CodegenTest, StrConstantPtrAndArray)
{
    std::string output = CompileToMadlen("char arr[] = \"ABC\"; char *p = \"ABC\";");
    EXPECT_EQ(R"(c
      arr:   ,name,
             ,log, 2024110300000000
             ,end,
c
        p:   ,name,
          13 ,z00,
             ,z00, *str0
    *str0:   ,log, 2024110300000000
             ,end,
)",
              output);
}

// An enumerator in a static initializer folds to its value before code generation.
// This is the reported repro: it used to abort in b6lower with
// "literal_to_int64: Cannot convert enum".  The emitted module must be identical
// to the one for "static const int a[] = { 0, 1 };".
TEST_F(CodegenTest, VarIntArrayInitEnumConst)
{
    std::string output = CompileToMadlen("enum { X, Y }; static const int a[] = { X, Y };");
    EXPECT_EQ(R"(c
        a:   ,name,
             ,log, 0
             ,log, 1
             ,end,
)",
              output);
}

// An enumerated-type struct member initialized by an enumerator: the member's
// TYPE_ENUM shares int's static-init representation (octal 52 = 42, 53 = 43).
TEST_F(CodegenTest, VarStructInitEnumConst)
{
    std::string output = CompileToMadlen(
        "enum E { A = 42, B }; struct S { int x; enum E e; }; struct S st = { A, B };");
    EXPECT_EQ(R"(c
       st:   ,name,
             ,log, 52
             ,log, 53
             ,end,
)",
              output);
}
