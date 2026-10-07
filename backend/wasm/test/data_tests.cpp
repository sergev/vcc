//
// wasm32 static data: each object in a section of its own, as clang has it, and
// reached by its relocated address.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// Initialized, zero and constant data, with their alignment and size.
TEST_F(WasmTest, StaticVariables)
{
    std::string s = CompileToWasm(R"(
        int g = 5;
        static short s = -2;
        long long ll = -3;
        char c = 'x';
        double d = 1.5;
        float f = 2.0f;
        int z;
        _Alignas(16) int al = 1;
        int main(void) { return g + s + c + z + al; }
    )");
    EXPECT_NE(s.find("\t.type\tg,@object\n\t.section\t.data.g,\"\",@\n\t.globl\tg\n"
                     "\t.p2align\t2, 0x0\ng:\n\t.int32\t5\n\t.size\tg, 4\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("\t.section\t.data.s,\"\",@\n\t.p2align\t1, 0x0\ns:\n\t.int16\t-2\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("ll:\n\t.int64\t-3\n"), std::string::npos) << s;
    EXPECT_NE(s.find("\t.section\t.data.c,\"\",@\n\t.globl\tc\nc:\n\t.int8\t120\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("d:\n\t.int64\t0x3ff8000000000000\n"), std::string::npos) << s;
    EXPECT_NE(s.find("f:\n\t.int32\t0x40000000\n"), std::string::npos) << s;
    EXPECT_NE(s.find("\t.section\t.bss.z,\"\",@\n\t.globl\tz\n\t.p2align\t2, 0x0\nz:\n"
                     "\t.skip\t4\n\t.size\tz, 4\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("\t.p2align\t4, 0x0\nal:\n"), std::string::npos) << s;
}

// A static object is read and written through its address: offset 0 plus the symbol.
EXPECT_CODE(StaticAccess,
            "i32.const 0\ni32.load8_s c\nlocal.set 0\ni32.const 0\nlocal.get 0\n"
            "i32.store g\nlocal.get 0\nreturn\nend_function\n",
            "char c; int g; int f(void) { g = c; return g; }")

// Run: globals, a static local that keeps its value, and an extern defined later.
TEST_F(WasmTest, RunStatics)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        extern int later;
        signed char c = -5;
        unsigned short us = 65535;
        long long big = 1LL << 40;
        int counter(void) { static int n = 10; return n++; }
        int main(void)
        {
            counter();
            counter();
            if (counter() != 12) return 1;
            if (c != -5 || us != 65535) return 2;
            c = (signed char)200;
            if (c != -56) return 3;
            if (big >> 40 != 1) return 4;
            return later;
        }
        int later = 33;
    )"));
    EXPECT_EQ(33, exit_status);
}
