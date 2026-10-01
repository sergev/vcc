//
// Static data: sections, alignment, initializers.
//
#include "riscv_test.h"

TEST_F(RiscvTest, DataInitializers)
{
    std::string s = CompileToRiscv(R"(
int g = -5;
static long z;
double d = 1.5;
char *msg = "a\"b";
short a[3] = { 1, 2 };
int *p = &g;
)");
    EXPECT_EQ(R"(    .data
    .globl  g
    .p2align 2
    .type   g, @object
    .size   g, 4
g:
    .word   -5
    .bss
    .p2align 3
    .type   z, @object
    .size   z, 8
z:
    .zero   8
    .data
    .globl  d
    .p2align 3
    .type   d, @object
    .size   d, 8
d:
    .dword  0x3ff8000000000000
    .section .rodata
    .p2align 0
    .type   _str0, @object
    .size   _str0, 4
_str0:
    .ascii  "a\"b"
    .byte   0
    .data
    .globl  msg
    .p2align 3
    .type   msg, @object
    .size   msg, 8
msg:
    .dword  _str0
    .data
    .globl  a
    .p2align 1
    .type   a, @object
    .size   a, 6
a:
    .half   1
    .half   2
    .zero   2
    .data
    .globl  p
    .p2align 3
    .type   p, @object
    .size   p, 8
p:
    .dword  g
)",
              s);
}

// A block-scope static follows its function, as a local symbol.
TEST_F(RiscvTest, DataStaticLocal)
{
    std::string s = CompileToRiscv("int f(void) { static int k = 3; return k++; }");
    EXPECT_NE(std::string::npos, s.find(R"(    .size   f, .-f
    .data
    .p2align 2
    .type   k, @object
    .size   k, 4
k:
    .word   3
)")) << s;
    EXPECT_NE(std::string::npos, Code(s).find("la t5, k\nlw t0, 0(t5)\n")) << s;
}
