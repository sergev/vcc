//
// wasm32 pointers, arrays and strings: addresses of slots, statics and functions;
// loads and stores in the pointed-to type; byte offsets scaled in ADD_PTR.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// A static array's address is its symbol; the element is a 16-bit signed load.
EXPECT_CODE(GlobalArrayIndex,
            "i32.const t\nlocal.set 1\nlocal.get 1\nlocal.get 0\ni32.const 2\ni32.mul\n"
            "i32.add\nlocal.set 2\nlocal.get 2\ni32.load16_s 0\nlocal.set 3\nlocal.get 3\n"
            "local.set 4\nlocal.get 4\nreturn\nend_function\n",
            "short t[10]; int f(int i) { return t[i]; }")

// A string literal is a read-only object of its own; plain char loads signed.
TEST_F(WasmTest, StringLiteral)
{
    std::string s = CompileToWasm("char f(int i) { return \"abc\"[i]; }");
    EXPECT_NE(s.find("\t.section\t.rodata._str0,\"\",@\n_str0:\n\t.ascii\t\"abc\"\n"
                     "\t.int8\t0\n\t.size\t_str0, 4\n"),
              std::string::npos)
        << s;
    EXPECT_NE(Code(s).find("i32.const _str0\n"), std::string::npos) << s;
    EXPECT_NE(Code(s).find("i32.load8_s 0\n"), std::string::npos) << s;
}

// A function's address is its index in the table, which the relocation gives.
EXPECT_CODE(FunctionAddress, "i32.const g\nlocal.set 0\nlocal.get 0\nreturn\nend_function\n",
            "int g(void); int (*f(void))(void) { return g; }")

// A constant stored into a member takes the member's width, not its own.
TEST_F(WasmTest, ConstantMemberStore)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm(R"(
        struct S { char c; short s; long long l; };
        long long f(void) { struct S x; x.c = 1; x.s = 2; x.l = 3; return x.c + x.s + x.l; }
    )"));
    EXPECT_NE(s.find("i32.const 1\ni32.store8 0\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i32.store16 2\n"), std::string::npos) << s;
    EXPECT_NE(s.find("i64.const 3\ni64.store 8\n"), std::string::npos) << s;
}

// Run: pointer arithmetic and differences, arrays of arrays, pointers to pointers.
TEST_F(WasmTest, RunPointers)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        long long g[5] = { 1, 2, 3, 4, 5 };
        int m[3][4];
        int main(void)
        {
            long long *p = g + 4, *q = &g[1];
            if (p - q != 3) return 1;
            if (*--p != 4 || p[-2] != 2) return 2;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    m[i][j] = i * 10 + j;
            int (*row)[4] = m + 2;
            if ((*row)[3] != 23 || m[1][2] != 12) return 3;
            int x = 5, *px = &x, **ppx = &px;
            **ppx += 1;
            if (x != 6) return 4;
            char buf[8];
            char *e = buf + sizeof buf;
            if (e - buf != 8) return 5;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: the string and memory routines of the runtime, and puts.
TEST_F(WasmTest, RunStringLibrary)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("helloo world\n", CompileAndRunWasm(R"(
        #include <stdio.h>
        #include <string.h>
        #include <stdlib.h>
        int main(void)
        {
            char buf[32];
            strcpy(buf, "hello");
            strcat(buf, ", world");
            if (strlen(buf) != 12) return 1;
            if (strcmp(buf, "hello, world") != 0 || strcmp("a", "b") >= 0) return 2;
            if (strchr(buf, ',') != buf + 5 || strstr(buf, "wor") != buf + 7) return 3;
            memset(buf + 20, 'x', 4);
            if (memcmp(buf + 20, "xxxx", 4) != 0) return 4;
            memmove(buf + 1, buf, 5); // overlapping: "hhello world"
            if (memcmp(buf, "hhello world", 13) != 0) return 5;
            memcpy(buf, "hello", 5); // "helloo world"
            if (atoi("-1234") != -1234) return 6;
            puts(buf);
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: the heap grows the linear memory past its first pages.
TEST_F(WasmTest, RunMalloc)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        #include <stdlib.h>
        #include <string.h>
        int main(void)
        {
            char *small = malloc(10);
            int *big = calloc(1000000, sizeof(int)); // 4 MB: memory.grow
            if (!small || !big) return 1;
            if (((unsigned long)small & 15) || ((unsigned long)big & 15)) return 2;
            for (int i = 0; i < 1000000; i += 1000)
                if (big[i]) return 3;
            big[999999] = 42;
            strcpy(small, "abc");
            char *more = realloc(small, 100);
            if (strcmp(more, "abc") != 0) return 4;
            return big[999999];
        }
    )"));
    EXPECT_EQ(42, exit_status);
}
