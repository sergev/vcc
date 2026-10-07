//
// wasm32 integer selection: operators and width conversions.  A narrow value is kept
// extended in its i32 by its type's signedness.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

EXPECT_CODE(TruncateToSignedChar,
            "local.get 0\ni32.extend8_s\nlocal.set 1\nlocal.get 1\nreturn\nend_function\n",
            "signed char f(int x) { return (signed char)x; }")
EXPECT_CODE(TruncateToUnsignedChar,
            "local.get 0\ni32.const 255\ni32.and\nlocal.set 1\nlocal.get 1\nreturn\n"
            "end_function\n",
            "unsigned char f(int x) { return x; }")
EXPECT_CODE(ZeroExtendToLongLong,
            "local.get 0\ni64.extend_i32_u\nlocal.set 1\nlocal.get 1\nreturn\nend_function\n",
            "unsigned long long f(unsigned x) { return x; }")
EXPECT_CODE(SignExtendToLongLong,
            "local.get 0\ni64.extend_i32_s\nlocal.set 1\nlocal.get 1\nreturn\nend_function\n",
            "long long f(int x) { return x; }")
EXPECT_CODE(TruncateLongLong,
            "local.get 0\ni32.wrap_i64\nlocal.set 1\nlocal.get 1\nreturn\nend_function\n",
            "int f(long long x) { return (int)x; }")

// The count of a shift has a type of its own, brought to the shifted value's.
EXPECT_CODE(ShiftLongLongByInt,
            "local.get 0\nlocal.get 1\ni64.extend_i32_u\ni64.shl\nlocal.set 2\nlocal.get 2\n"
            "return\nend_function\n",
            "long long f(long long x, int n) { return x << n; }")
EXPECT_CODE(NotLongLong, "local.get 0\ni64.eqz\nlocal.set 1\nlocal.get 1\nreturn\nend_function\n",
            "int f(long long x) { return !x; }")
EXPECT_CODE(Negate,
            "i32.const 0\nlocal.get 0\ni32.sub\nlocal.set 1\nlocal.get 1\nreturn\nend_function\n",
            "int f(int x) { return -x; }")

// unsigned short arithmetic is int arithmetic, masked back to 16 bits on the way out.
EXPECT_CODE(UnsignedShortMultiply,
            "local.get 0\nlocal.set 2\nlocal.get 1\nlocal.set 3\nlocal.get 2\nlocal.get 3\n"
            "i32.mul\nlocal.set 4\nlocal.get 4\ni32.const 65535\ni32.and\nlocal.set 5\n"
            "local.get 5\nreturn\nend_function\n",
            "unsigned short f(unsigned short a, unsigned short b) { return a * b; }")

// Signed and unsigned division, remainder, comparison and shift pick their own opcodes.
TEST_F(WasmTest, SignedAndUnsignedOps)
{
    std::string s = Code(CompileToWasm(R"(
        int sd(int a, int b) { return a / b; }
        unsigned ud(unsigned a, unsigned b) { return a / b; }
        int sr(int a, int b) { return a % b; }
        unsigned ur(unsigned a, unsigned b) { return a % b; }
        int sl(int a, int b) { return a < b; }
        int ul(unsigned a, unsigned b) { return a < b; }
        int ss(int a, int b) { return a >> b; }
        unsigned us(unsigned a, int b) { return a >> b; }
        long long ld(long long a, long long b) { return a / b; }
        unsigned long long lu(unsigned long long a, unsigned long long b) { return a % b; }
    )"));
    for (const char *op : { "i32.div_s", "i32.div_u", "i32.rem_s", "i32.rem_u", "i32.lt_s",
                            "i32.lt_u", "i32.shr_s", "i32.shr_u", "i64.div_s", "i64.rem_u" })
        EXPECT_NE(s.find(std::string(op) + "\n"), std::string::npos) << op << "\n" << s;
}

// Run: the narrow types wrap and extend as C says.
TEST_F(WasmTest, RunNarrowWrap)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        int main(void)
        {
            signed char c = 127;
            unsigned char u = 255;
            short s = 32767;
            unsigned short us = 0;
            c++;
            u++;
            s++;
            us--;
            if (c != -128) return 1;
            if (u != 0) return 2;
            if (s != -32768) return 3;
            if (us != 65535) return 4;
            if ((unsigned)(signed char)200 != 4294967240u) return 5;
            if ((int)(unsigned char)-1 != 255) return 6;
            if ((char)'\x80' >= 0) return 7; // plain char is signed
            return 42;
        }
    )"));
    EXPECT_EQ(42, exit_status);
}

// Run: long long arithmetic in i64, with unsigned division and a long count.
TEST_F(WasmTest, RunLongLong)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        long long mul(long long a, long long b) { return a * b; }
        unsigned long long udiv(unsigned long long a, unsigned long long b) { return a / b; }
        long long shr(long long a, long long n) { return a >> n; }
        int main(void)
        {
            if (mul(1LL << 32, 3) != 12884901888LL) return 1;
            if (udiv(18446744073709551615ULL, 2) != 9223372036854775807ULL) return 2;
            if (shr(-1LL << 40, 39) != -2) return 3;
            if ((int)(mul(0x100000001LL, 1)) != 1) return 4;
            if (-5LL % 3 != -2) return 5;
            return 9;
        }
    )"));
    EXPECT_EQ(9, exit_status);
}

// Run: unsigned comparisons and shifts.
TEST_F(WasmTest, RunUnsigned)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        int lt(unsigned a, unsigned b) { return a < b; }
        unsigned shr(unsigned a, int n) { return a >> n; }
        int sar(int a, int n) { return a >> n; }
        int main(void)
        {
            if (lt(4294967295u, 1)) return 1;
            if (shr(0x80000000u, 31) != 1) return 2;
            if (sar(-8, 1) != -4) return 3;
            if (4294967295u / 2 != 2147483647u) return 4;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
