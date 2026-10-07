//
// wasm32 code generation: golden instruction sequences and the declarations of a unit.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// main(void) is __original_main, beside clang's main(argc, argv) that calls it and the
// __main_void alias that crt0 calls.
TEST_F(WasmTest, MainVoid)
{
    std::string s = CompileToWasm("int main(void) { return 42; }");
    EXPECT_NE(s.find("\t.functype\t__original_main () -> (i32)\n"
                     "\t.functype\tmain (i32, i32) -> (i32)\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("__original_main:\n"
                     "\t.functype\t__original_main () -> (i32)\n"
                     "\ti32.const\t42\n"
                     "\treturn\n"
                     "\tend_function\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("main:\n"
                     "\t.functype\tmain (i32, i32) -> (i32)\n"
                     "\tcall\t__original_main\n"
                     "\tend_function\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("__main_void = __original_main\n"), std::string::npos) << s;
}

// main(argc, argv) is __main_argc_argv, and nothing else.
TEST_F(WasmTest, MainArgcArgv)
{
    std::string s = CompileToWasm("int main(int argc, char **argv) { return argc; }");
    EXPECT_NE(s.find("__main_argc_argv:\n"
                     "\t.functype\t__main_argc_argv (i32, i32) -> (i32)\n"
                     "\tlocal.get\t0\n"
                     "\treturn\n"),
              std::string::npos)
        << s;
    EXPECT_EQ(s.find("\nmain:"), std::string::npos) << s;
    EXPECT_EQ(s.find("__main_void"), std::string::npos) << s;
}

// The features clang records with Braam's flags, so that wasm-ld links our objects
// with its.
TEST_F(WasmTest, TargetFeatures)
{
    std::string s = CompileToWasm("int main(void) { return 0; }");
    EXPECT_NE(s.find("\t.section\t.custom_section.target_features,\"\",@\n"
                     "\t.int8\t8\n"
                     "\t.int8\t43\n"
                     "\t.int8\t11\n"
                     "\t.ascii\t\"bulk-memory\"\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("\t.ascii\t\"sign-ext\"\n"), std::string::npos) << s;
}

// Constants of each value type, in the function's result type.
EXPECT_CODE(ReturnLongLong, "i64.const 1099511627776\nreturn\nend_function\n",
            "long long f(void) { return 1LL << 40; }")
EXPECT_CODE(ReturnDouble, "f64.const 0x1.4p+1\nreturn\nend_function\n",
            "double f(void) { return 2.5; }")
EXPECT_CODE(ReturnFloat, "f32.const 0x1.4p+1\nreturn\nend_function\n",
            "float f(void) { return 2.5f; }")
EXPECT_CODE(ReturnSignedChar, "i32.const -3\nreturn\nend_function\n",
            "signed char f(void) { return -3; }")
EXPECT_CODE(ReturnUnsigned, "i32.const -1\nreturn\nend_function\n",
            "unsigned f(void) { return 4294967295u; }")
EXPECT_CODE(ReturnParam, "local.get 1\nreturn\nend_function\n",
            "long long f(int a, long long b) { return b; }")
EXPECT_CODE(ReturnVoid, "return\nend_function\n", "void f(void) { return; }")

// The variable arguments come in a buffer: one more i32 parameter, its address.
TEST_F(WasmTest, VariadicSignature)
{
    std::string s = CompileToWasm("int f(int n, ...) { return n; }");
    EXPECT_NE(s.find("f:\n\t.functype\tf (i32, i32) -> (i32)\n"), std::string::npos) << s;
}
