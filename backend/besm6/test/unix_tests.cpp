//
// Golden-file tests for the Unix (b6as) assembler emitter (emit_unix.c).
//
// These assert the exact `.s` text produced for a representative set (scalar, call,
// global, float const, string, static array, pointer relocation) — the U1 acceptance
// criteria.  The `$` in helper names (b$save, b$ret) passes through the Unix sanitizer
// unchanged, whereas the Madlen emitter lowers it to `/`.
//
// Listing format: lines indent with 4 spaces; an instruction's leading modreg occupies a
// ` NN ` field (space + register right-aligned to width 2 + space) — exactly 4 columns, so
// mnemonics align at column 4 with or without a register.
//
#include "codegen_test.h"

// A scalar-returning function: prologue (its/b$save), param load, `#`-pool constant, epilogue.
TEST_F(CodegenTest, UnixScalarReturn)
{
    std::string out = CompileToUnix("int f(int x) { return x + 1; }");
    EXPECT_EQ(R"(    .text
    .globl f
f:
    its 13
 13 vjm b$save
  6 xta
    a+x #1
    uj b$ret
)",
              out);
}

// A direct call: ` 13 vjm g` (link register r13); b6as auto-externs the undefined callee.
TEST_F(CodegenTest, UnixCall)
{
    std::string out = CompileToUnix("int g(int); int f(int x) { return g(x) + 1; }");
    EXPECT_EQ(R"(    .text
    .globl f
f:
    its 13
 13 vjm b$save
  6 xta
 14 vtm -1
 13 vjm g
    a+x #1
    uj b$ret
)",
              out);
}

// A module-level global: an all-zero tentative definition becomes a `.comm` common symbol
// (so repeated tentative defs merge at link time); the accessor reaches it via `utc counter`
// + bare `xta`.
TEST_F(CodegenTest, UnixGlobalAccess)
{
    std::string out = CompileToUnix("int counter; int f(void) { return counter; }");
    EXPECT_EQ(R"(    .comm counter, 1
    .text
    .globl f
f:
    its 13
 13 vjm b$save0
    utc counter
    xta
    uj b$ret
)",
              out);
}

// A floating constant flows through the `#`-pool.  b6as has no float literal syntax, so
// the real is rendered as its native BESM-6 48-bit FP bit pattern in octal (3.14).
TEST_F(CodegenTest, UnixFloatConst)
{
    std::string out = CompileToUnix("double f(void) { return 3.14; }");
    EXPECT_EQ(R"(    .text
    .globl f
f:
    its 13
 13 vjm b$save0
    xta #04114436560507534
    uj b$ret
)",
              out);
}

// A static array: one octal `.word` per element in .data.
TEST_F(CodegenTest, UnixStaticArray)
{
    std::string out = CompileToUnix("int a[3] = {10, 20, 30};");
    EXPECT_EQ(R"(    .data
    .globl a
a:
    .word 012
    .word 024
    .word 036
)",
              out);
}

// A string-valued pointer: a fat pointer (byte-offset marker 13 on the leading modreg)
// emitted as a raw `@00` pair, followed by the folded KOI-7 string constant.
TEST_F(CodegenTest, UnixStringGlobal)
{
    std::string out = CompileToUnix("char *msg = \"HI\";");
    EXPECT_EQ(R"(    .data
    .globl msg
msg:
 13 @00
    @00 _str0
_str0:
    .word 02204440000000000
)",
              out);
}

// A plain pointer to a global: the Z00 pair coalesces into one relocatable `.word g`.
TEST_F(CodegenTest, UnixPointerToGlobal)
{
    std::string out = CompileToUnix("int g; int *p = &g;");
    EXPECT_EQ(R"(    .comm g, 1
    .data
    .globl p
p:
    .word g
)",
              out);
}

// A double global: its native-FP value renders as a `.word` holding the BESM-6 48-bit FP
// bit pattern in octal (2.5).
TEST_F(CodegenTest, UnixDoubleGlobal)
{
    std::string out = CompileToUnix("double d = 2.5;");
    EXPECT_EQ(R"(    .data
    .globl d
d:
    .word 04112000000000000
)",
              out);
}

// A Madlen left-aligned literal (`=:64`, the INT-format exponent word) becomes a b6as
// `#`-pool constant using the prefix-apostrophe left-align form (`#0'64`); b6as has no
// `=` literal syntax.
TEST_F(CodegenTest, UnixIntFormatLiteral)
{
    std::string out = CompileToUnix("double g(int n) { return n; }");
    EXPECT_EQ(R"(    .text
    .globl g
g:
    its 13
 13 vjm b$save
  6 xta
    aox #0'64
    ntr 0
    a+x
    ntr 7
    uj b$ret
)",
              out);
}

// A numeric compiler temporary (`%0`) sanitizes to `.T0`, not `.0` — a bare `.N` is a
// b6as bit-mask literal, so a digit-leading body gets a `T` inserted to stay a name.
TEST_F(CodegenTest, UnixNumericLabel)
{
    std::string out = CompileToUnix("int f(int x) { if (x) return 1; return 0; }");
    EXPECT_EQ(R"(    .text
    .globl f
f:
    its 13
 13 vjm b$save
  6 xta
    uza .T0
    xta #1
    uj b$ret
.T0:
    xta
    uj b$ret
)",
              out);
}

// Regression (task U7): compiler-internal branch labels (`.TN`) must stay unique
// across every function in one Unix assembly file.  Temps/labels are numbered per
// translation unit (translate.h), so `f`'s `if` label is `.T0` and `g`'s is `.T2`
// — never both `.T0`.  Before the fix each function restarted numbering at `.T0`;
// b6as silently accepts the duplicate and binds a branch to the wrong function's
// label, which sent `doprnt`'s format walk into an infinite loop under b6sim.
TEST_F(CodegenTest, UnixLocalLabelsUniqueAcrossFunctions)
{
    std::string out = CompileToUnix(
        "int f(int x) { if (x) return 1; return 2; }\n"
        "int g(int y) { if (y) return 3; return 4; }");
    EXPECT_EQ(R"(    .text
    .globl f
f:
    its 13
 13 vjm b$save
  6 xta
    uza .T0
    xta #1
    uj b$ret
.T0:
    xta #2
    uj b$ret
    .text
    .globl g
g:
    its 13
 13 vjm b$save
  6 xta
    uza .T2
    xta #3
    uj b$ret
.T2:
    xta #4
    uj b$ret
)",
              out);
}

// A `static` function has internal linkage, so its label must not be preceded by `.globl`
// — b6as would otherwise export it, letting two same-named statics in different objects
// collide in b6ld.  The intra-object ` 13 vjm helper` still resolves against the local label.
TEST_F(CodegenTest, UnixStaticFunction)
{
    std::string out = CompileToUnix(
        "static int helper(int x) { return x + 1; }\n"
        "int caller(int y) { return helper(y); }");
    EXPECT_EQ(R"(    .text
helper:
    its 13
 13 vjm b$save
  6 xta
    a+x #1
    uj b$ret
    .text
    .globl caller
caller:
    its 13
 13 vjm b$save
  6 xta
 14 vtm -1
 13 vjm helper
    uj b$ret
)",
              out);
}

// A `static` file-scope variable is likewise unexported.  The tentative one (`pending`, no
// initializer) stays a strong .bss label rather than becoming a `.comm` common — merging
// across objects is only correct for external linkage.
TEST_F(CodegenTest, UnixStaticVariable)
{
    std::string out = CompileToUnix(
        "static int counter = 5;\n"
        "static int pending;\n"
        "int bump(void) { return counter + pending; }");
    EXPECT_EQ(R"(    .data
counter:
    .word 5
    .bss
pending:
    . = . + 1
    .text
    .globl bump
bump:
    its 13
 13 vjm b$save0
    utc counter
    xta
    utc pending
    a+x
    uj b$ret
)",
              out);
}

TEST_F(CodegenTest, UnixStructFieldPtrInit)
{
    std::string output = CompileToUnix(R"(
        struct foo {
            int a;
            int b;
        };
        struct foo bar = { 1, 2 };
        int *quz = &bar.b;
)");
    // `bar` and `quz` are separate modules in the Unix dialect, so each re-emits its own
    // `.data` segment directive (harmless/idempotent for b6as).
    EXPECT_EQ(R"(    .data
    .globl bar
bar:
    .word 1
    .word 2
    .data
    .globl quz
quz:
    .word bar+1
)",
              output);
}

// Unlabeled zero data words pack up to 8 per `.word` line.
TEST_F(CodegenTest, UnixZeroWordsPacked)
{
    std::string out = CompileToUnix("int u[12] = { 0, 0, 5 };");
    EXPECT_EQ(R"(    .data
    .globl u
u:
    .word 0, 0
    .word 5
    .word 0, 0, 0, 0, 0, 0, 0, 0
    .word 0
)",
              out);
}
