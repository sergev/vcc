//
// AVR static data, global access and addresses: data addresses are bytes in SRAM,
// function addresses words in flash.
//
#include "avr_test.h"

// The data of a translation unit, from the first section on, without the functions.
static std::string Data(const std::string &asm_text)
{
    std::string out;
    bool in_text = false;
    for (size_t pos = 0, nl; pos < asm_text.size(); pos = nl + 1) {
        nl               = asm_text.find('\n', pos);
        std::string line = asm_text.substr(pos, nl - pos);
        if (line == "    .text")
            in_text = true;
        else if (line.compare(0, 5, "    .") == 0 && line.find("section") != std::string::npos)
            in_text = false;
        else if (line == "    .data" || line == "    .bss")
            in_text = false;
        if (!in_text && line.find(" = ") == std::string::npos)
            out += line + "\n";
    }
    return out;
}

TEST_F(AvrTest, StaticScalars)
{
    NaiveSelection();
    EXPECT_EQ(R"(    .data
    .globl  i
    .type   i, @object
    .size   i, 2
i:
    .short  -2
    .data
    .globl  l
    .type   l, @object
    .size   l, 4
l:
    .long   100000
    .data
    .type   ll, @object
    .size   ll, 8
ll:
    .quad   -5000000000
    .bss
    .globl  z
    .type   z, @object
    .size   z, 3
z:
    .zero   3
    .data
    .globl  d
    .type   d, @object
    .size   d, 4
d:
    .long   0x3fc00000
)",
              Data(CompileToAvr(R"(int i = -2; long l = 100000; static long long ll = -5000000000LL;
char z[3]; double d = 1.5;
long long *use(void) { return &ll; })")));
}

// A data pointer is .short sym+off; a function pointer .short pm(f), a word address.
TEST_F(AvrTest, StaticPointers)
{
    NaiveSelection();
    std::string s =
        Data(CompileToAvr("int a[4]; int *p = &a[2];\n"
                          "int f(void);\nint (*fp)(void) = f;\n"
                          "const char *s = \"hi\";"));
    EXPECT_NE(std::string::npos, s.find(R"(p:
    .short  a+4
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(fp:
    .short  pm(f)
)")) << s;
    EXPECT_NE(std::string::npos, s.find("    .section .rodata\n")) << s;
    EXPECT_NE(std::string::npos, s.find("    .ascii  \"hi\"\n    .byte   0\n")) << s;
}

// Globals are read and written with lds/sts.
EXPECT_CODE(GlobalAccess, R"(lds r22, g
lds r23, g+1
lds r24, g+2
lds r25, g+3
sts h, r22
sts h+1, r23
sts h+2, r24
sts h+3, r25
)",
            "long g, h;\nvoid f(void) { h = g; }")

// The address of data is lo8/hi8, of a function pm_lo8/pm_hi8.
TEST_F(AvrTest, Addresses)
{
    NaiveSelection();
    std::string s = Code(CompileToAvr(R"(int g;
int h(void);
int *f(void) { return &g; }
int (*k(void))(void) { return h; })"));
    EXPECT_NE(std::string::npos, s.find(R"(ldi r24, lo8(g)
ldi r25, hi8(g)
)")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(ldi r24, pm_lo8(h)
ldi r25, pm_hi8(h)
)")) << s;
}

// Globals and static locals, initialized and not, read and written.
TEST_F(AvrTest, RunGlobals)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
int i = 1000;
long l = -100000;
long long ll;
unsigned char c = 200;
int count(void) { static int n = 10; return n++; }
int main(void)
{
    if (i != 1000 || l != -100000 || ll != 0 || c != 200) return 1;
    i = i + 1;
    l = l - 1;
    ll = 0x123456789LL;
    c = c + 100;
    if (i != 1001 || l != -100001 || ll != 0x123456789LL || c != 44) return 2;
    count();
    count();
    if (count() != 12) return 3;
    return 0;
}
)"));
}

// Function pointers: a word address, in a global and a local, called with icall.
TEST_F(AvrTest, RunFunctionPointers)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0\n", CompileAndRunBook(R"(
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int (*op)(int, int) = add;
int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
int main(void)
{
    if (op(2, 3) != 5) return 1;
    op = sub;
    if (op(2, 3) != -1) return 2;
    int (*local)(int, int) = add;
    if (apply(local, 30, 12) != 42) return 3;
    return 0;
}
)"));
}

// Globals and function pointers shared with clang's code, both ways.
TEST_F(AvrTest, RunDataWithClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    SKIP_IF_NO_AVR_CLANG();
    EXPECT_EQ("ok", CompileAndRunWithClang(R"(
void putbyte(int c);
long ours = 123456;
extern long theirs;
int twice(int x) { return x + x; }
int call_theirs(int (*f)(int), int x);
int (*get_theirs(void))(int);
int main(void)
{
    if (theirs != -654321) return 1;
    if (call_theirs(twice, 21) != 42) return 2;
    if (get_theirs()(5) != 6) return 3;
    putbyte('o');
    putbyte('k');
    return 0;
}
)",
                                           R"(
extern long ours;
long theirs = -654321;
int call_theirs(int (*f)(int), int x) { return ours == 123456 ? f(x) : -1; }
static int inc(int x) { return x + 1; }
int (*get_theirs(void))(int) { return inc; }
)"));
    EXPECT_EQ(0, exit_status);
}
