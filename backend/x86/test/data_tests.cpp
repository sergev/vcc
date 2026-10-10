//
// x86-64 static data: sections and directives, and globals accessed as sym(%rip).
//
#include "x86_test.h"

TEST_F(X86Test, GlobalData)
{
    std::string s = CompileToX86(R"(
int counter = 5;
static long zeros[4];
const char *name = "hi";
short sh = -2;
unsigned char uc = 200;
double d = 1.5;
float fl = 0.5f;
int *p = &counter + 1;
)");
    for (const char *want :
         { "    .data\n    .globl  counter\n    .p2align 2\n    .type   counter, @object\n"
           "    .size   counter, 4\ncounter:\n    .long   5\n",
           "    .bss\n    .p2align 3\n    .type   zeros, @object\n    .size   zeros, 32\n"
           "zeros:\n    .zero   32\n",
           "    .short  -2\n", "    .byte   200\n", "    .quad   0x3ff8000000000000\n",
           "    .long   0x3f000000\n", "    .quad   counter+4\n", "    .ascii  \"hi\"\n" })
        EXPECT_NE(std::string::npos, s.find(want)) << want << " in\n" << s;
    EXPECT_NE(std::string::npos, s.find("    .section .rodata\n")) << s;
}

// A long double is the x87 format: 64-bit significand with its explicit integer bit,
// sign and exponent, then six bytes of padding.
TEST_F(X86Test, LongDoubleData)
{
    std::string s = CompileToX86("long double x = 1.0L; long double y = -0.1L;");
    EXPECT_NE(std::string::npos, s.find("    .p2align 4\n")) << s;
    EXPECT_NE(std::string::npos,
              s.find("x:\n    .quad   0x8000000000000000\n    .short  0x3fff\n    .zero   6\n"))
        << s;
    EXPECT_NE(std::string::npos,
              s.find("y:\n    .quad   0xcccccccccccccccd\n    .short  0xbffb\n    .zero   6\n"))
        << s;
}

// A global is read and written as sym(%rip).
TEST_F(X86Test, GlobalAccess)
{
    NaiveSelection();
    std::string code = Code(CompileToX86("int g; int f(void) { g = g + 1; return g; }"));
    EXPECT_NE(std::string::npos, code.find("movl g(%rip), %eax\naddl $1, %eax\n")) << code;
    EXPECT_NE(std::string::npos, code.find("movl %eax, g(%rip)\n")) << code;
}

// Static locals of the same name stay apart: the later one is name$N, which both
// assemblers take as a symbol.
TEST_F(X86Test, StaticLocals)
{
    std::string s = CompileToX86(R"(
int f(void) { static int n = 1; n = n + 1; return n; }
int g(void) { static int n = 7; return n; }
)");
    EXPECT_NE(std::string::npos, s.find("n:\n    .long   1\n")) << s;
    EXPECT_NE(std::string::npos, s.find("n$1:\n    .long   7\n")) << s;
    EXPECT_NE(std::string::npos, Code(s).find("movl n$1(%rip), %eax\n")) << s;
}

TEST_F(X86Test, RunGlobals)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
long total = 100;
static signed char small = -5;
unsigned short big = 65535;
int bump(void) {
    static int calls;
    calls = calls + 1;
    return calls;
}
int main(void) {
    bump();
    bump();
    total = total + small + big - bump();
    return total - 65535;   // 100 - 5 + 65535 - 3 - 65535
})");
    EXPECT_EQ(92, exit_status);
}
