#include "codegen_test.h"

//
// CopyToOffset / CopyFromOffset — aggregate member access on a named struct
// variable (task #20).
//
// `s.field` (read) lowers to copy_from_offset; `s.field = …` (write) lowers to
// copy_to_offset.  The instruction carries a *byte* offset; for the besm6 target a
// word-sized member (int/long/pointer/float/double) is 6 bytes, so the backend
// addresses the member at word offset = byte_offset / 6.  Pointer-based access
// (`p->field`) is handled by ADD_PTR + LOAD/STORE instead (task #19).
//
// A local aggregate (struct/array) reserves a contiguous multi-word frame slot via
// an AllocateLocal pseudo-instruction (task #23), so its members no longer overlap
// adjacent auto slots.  The frame prologue (15 ,utm, N) extends the stack by the full
// aggregate size, and member access addresses word offset = byte_offset / 6.
//

// Local struct, write second member: the 2-word slot extends the frame by 2 words
// (15 ,utm, 2); s.y is word offset 1 of the slot at offset 0 -> 7 ,atx, 1.
TEST_F(CodegenTest, CopyToOffsetLocalWrite)
{
    std::string output =
        CompileToMadlen("struct Foo { int x; int y; }; void f(void){ struct Foo s; s.y = 5; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
          15 ,utm, 2
             ,xta, =5
           7 ,atx, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

// Local struct, read second member: the 2-word slot occupies offsets 0..1 and the
// return temp lands at offset 2, so the frame extends by 3 words (15 ,utm, 3).
TEST_F(CodegenTest, CopyFromOffsetLocalRead)
{
    std::string output =
        CompileToMadlen("struct Foo { int x; int y; }; int f(void){ struct Foo s; return s.y; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
          15 ,utm, 2
           7 ,xta, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

// Global struct: ,utc, g then ,atx, 1 / ,xta, 1, plus a ,subp, g declaration.
TEST_F(CodegenTest, CopyOffsetGlobalStruct)
{
    std::string output = CompileToMadlen(
        "struct Foo { int x; int y; }; struct Foo g; int f(void){ g.x = 7; return g.y; }");
    EXPECT_EQ(R"(c
        g:   ,name,
             ,bss, 2
             ,end,
c
        f:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta, =7
             ,utc, g
             ,atx,
             ,utc, g
             ,xta, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

// A packed char member of a *global* struct: emit_member_fatptr builds the member's fat
// pointer with a single `14 ,vtm, s+1` — the base label and the member's word offset both
// ride in VTM's own 15-bit address field, so the pair `,utc, s+1` / `14 ,vtm, 0` collapses.
// (A local base still needs the UTC: its index register has nowhere else to go.)
TEST_F(CodegenTest, CopyByteToOffsetGlobalStruct)
{
    std::string output =
        CompileToMadlen("struct S { int a; char c; }; struct S s; void f(void){ s.c = 'Q'; }");
    EXPECT_EQ(R"(c
        s:   ,name,
             ,bss, 2
             ,end,
c
        f:   ,name,
    b/ret:   ,subp,
        s:   ,subp,
             ,its, 13
             ,call, b/save0
          14 ,vtm, s+1
             ,ita, 14
             ,aox, =:64
             ,xts, =121
             ,call, b/stb
             ,uj, b/ret
             ,end,
)",
              output);
}

// Runtime: write both members of a global struct, read them back and add.
TEST_F(CodegenTest, CopyOffsetGlobalRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct Foo { int x; int y; };
        struct Foo g;
        void program() {
            g.x = 7;
            g.y = 35;
            printf("%d\n", g.x + g.y);
        }
    )");
    EXPECT_EQ("42\n", result);
}

// Runtime: a *local* struct now occupies a contiguous 2-word slot (task #23), so
// writing both members and reading them back yields independent values — they no
// longer overlap.  Returns 18 = 7 + 11.
TEST_F(CodegenTest, LocalStructRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct Foo { int x; int y; };
        void program() {
            struct Foo s;
            s.x = 7;
            s.y = 11;
            printf("%d\n", s.x + s.y);
        }
    )");
    EXPECT_EQ("18\n", result);
}

// Returning a 2-word struct by value from a direct call: the hidden-pointer (sret) ABI
// carries both members back into the caller's slot.  Output: "3 4\n".
TEST_F(CodegenTest, StructByValueReturnRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct P { int x; int y; };
        struct P mk(int a, int b) { struct P p; p.x = a; p.y = b; return p; }
        void program() {
            struct P r = mk(3, 4);
            printf("%d %d\n", r.x, r.y);
        }
    )");
    EXPECT_EQ("3 4\n", result);
}

// Whole-struct assignment (struct b = a;) copies every word.  Output: "5 9\n".
TEST_F(CodegenTest, StructWholeCopyRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct P { int x; int y; };
        void program() {
            struct P a;
            a.x = 5;
            a.y = 9;
            struct P b;
            b = a;
            printf("%d %d\n", b.x, b.y);
        }
    )");
    EXPECT_EQ("5 9\n", result);
}

// A 3-word struct returned by value, plus the result used directly to initialize
// another local.  Output: "1 2 3\n".
TEST_F(CodegenTest, StructByValueReturnThreeWordRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct T { int a; int b; int c; };
        struct T mk(void) { struct T t; t.a = 1; t.b = 2; t.c = 3; return t; }
        void program() {
            struct T r = mk();
            printf("%d %d %d\n", r.a, r.b, r.c);
        }
    )");
    EXPECT_EQ("1 2 3\n", result);
}

// A tagless struct shared by a comma-separated declarator list, end to end: `g = h` at file
// scope and `n = m` at block scope both copy every word.  The two declarators must name one
// type -- while the synthetic tag was minted per cloned declarator type, this failed to
// typecheck ("cannot convert '.*' to '.*' when").  Output: "5 9 7\n".
TEST_F(CodegenTest, AnonStructDeclaratorListRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        struct { int x; int y; } g, h;
        void program() {
            h.x = 5;
            h.y = 9;
            g = h;
            struct { int v; } m, n;
            m.v = 7;
            n = m;
            printf("%d %d %d\n", g.x, g.y, n.v);
        }
    )");
    EXPECT_EQ("5 9 7\n", result);
}
