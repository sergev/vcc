//
// MMIX static data: the directives (big-endian, so they carry the byte order), the
// sections, and the code that reaches data: ldo/sto/lda through linker-allocated base
// registers, geta for .rodata and functions.
//
#include "mmix_test.h"

// Every initializer kind, with the padding between members.
TEST_F(MmixTest, DataDirectives)
{
    std::string s = CompileToMmix(R"(
        long f(void);
        struct M { char c; short s; int i; long l; float x; double d; } m = { 1, -2, 3, -4, 1.5f, 2.5 };
        long (*fp)(void) = f;
        int k[3] = { 1, 2, 3 };
        int *pk = &k[1];
        long f(void) { return 0; }
    )");
    EXPECT_NE(std::string::npos, s.find("\t.data\n"
                                        "\t.global\tm\n"
                                        "\t.p2align 3\n"
                                        "m:\n"
                                        "\t.byte\t1\n"
                                        "\t.zero\t1\n"
                                        "\t.short\t-2\n"
                                        "\t.long\t3\n"
                                        "\t.quad\t-4\n"
                                        "\t.long\t0x3fc00000\n"
                                        "\t.zero\t4\n"
                                        "\t.quad\t0x4004000000000000\n"))
        << s;
    EXPECT_NE(std::string::npos, s.find("fp:\n\t.quad\tf\n")) << s;
    EXPECT_NE(std::string::npos, s.find("pk:\n\t.quad\tk+4\n")) << s;
}

// Zeros go to .bss; a string literal to .rodata, 4-aligned for geta.
TEST_F(MmixTest, DataSections)
{
    std::string s = CompileToMmix(R"(
        long z;
        static char buf[10];
        const char *f(void) { return "hi"; }
    )");
    EXPECT_NE(std::string::npos, s.find("\t.bss\n\t.global\tz\n\t.p2align 3\nz:\n\t.zero\t8\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\t.bss\nbuf:\n\t.zero\t10\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\t.section .rodata\n\t.p2align 2\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\t.ascii\t\"hi\"\n\t.byte\t0\n")) << s;
}

// A global is reached by name: the assembler and linker supply a base register.
TEST_F(MmixTest, GlobalAccess)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        long g;
        int h;
        long f(void) { g = g + 1; return h; }
    )"));
    EXPECT_NE(std::string::npos, code.find("ldo $248,g\naddu $248,$248,1\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sto $248,g\n")) << code;
    EXPECT_NE(std::string::npos, code.find("ldt $248,h\n")) << code;
}

// An address: lda for data, geta for a function (as GCC takes it) and for .rodata.
TEST_F(MmixTest, Addresses)
{
    NaiveSelection();
    std::string code = Code(CompileToMmix(R"(
        long g;
        long h(void);
        long *f1(void) { return &g; }
        long (*f2(void))(void) { return h; }
    )"));
    EXPECT_NE(std::string::npos, code.find("lda $248,g\n")) << code;
    EXPECT_NE(std::string::npos, code.find("geta $248,h\n")) << code;
}

// A block-scope static is emitted after its function, under its name$N when another
// function has one of the same name; GNU as takes the $.
TEST_F(MmixTest, StaticLocals)
{
    std::string s = CompileToMmix(R"(
        long f(void) { static long n = 5; n = n + 1; return n; }
        long g(void) { static long n; n = n + 2; return n; }
    )");
    EXPECT_NE(std::string::npos, s.find("\t.data\n\t.p2align 3\nn:\n\t.quad\t5\n")) << s;
    EXPECT_NE(std::string::npos, s.find("\t.bss\n\t.p2align 3\nn$1:\n\t.zero\t8\n")) << s;
}

// Run: initialized data of every width read back, the byte order big-endian; static
// locals keep their values; a function pointer and a data pointer from data.
TEST_F(MmixTest, RunStaticData)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        signed char c = -5;
        unsigned short us = 65000;
        int i = -100000;
        unsigned u = 4000000000u;
        long l = -5000000000L;
        long z;
        long seven(void) { return 7; }
        long (*fp)(void) = seven;
        long *pz = &z;
        long count(void) { static long n = 10; n = n + 1; return n; }
        long other(void) { static long n; n = n + 100; return n; }
        int main(void)
        {
            if (c != -5) return 1;
            if (us != 65000) return 2;
            if (i != -100000) return 3;
            if (u != 4000000000u) return 4;
            if (l != -5000000000L) return 5;
            if (z != 0) return 6;
            if (fp() != 7) return 7;
            if (pz != &z) return 8;
            count();
            other();
            if (count() != 12) return 9;
            if (other() != 200) return 10;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// The base-register budget: the linker allocates one global register per 256-byte
// window of data that code reaches by name, from $246 down to $32, 215 in all with
// crt0's eight reserved.  Measured by hand: 215 separate windows link and run, and one
// more fails the link ("too many global registers: 224, max 223"), never silently.
// This run stays below, with 200.
TEST_F(MmixTest, RunBaseRegisterBudget)
{
    SKIP_IF_NO_MMIX_TOOLS();
    int n = 200;
    std::string src;
    for (int k = 0; k < n; k++)
        src += "long g" + std::to_string(k) + " = " + std::to_string(k) + "; char f" +
               std::to_string(k) + "[300] = {1};\n";
    src += "int main(void) { long s = 0;\n";
    for (int k = 0; k < n; k++)
        src += "s = s + g" + std::to_string(k) + ";\n";
    src += "return s == " + std::to_string(n * (n - 1) / 2) + " ? 0 : 1; }\n";
    EXPECT_EQ("", CompileAndRunMmix(src));
    EXPECT_EQ(0, exit_status);
}
