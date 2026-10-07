//
// wasm32 run tests: programs linked with the runtime and run under node.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// main's result is the exit status, and nothing is printed.
TEST_F(WasmTest, RunReturnStatus)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm("int main(void) { return 7; }"));
    EXPECT_EQ(7, exit_status);
}

// A status of 255 from a clean exit: run.mjs's report tells it from a trap.
TEST_F(WasmTest, RunReturn255)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm("int main(void) { return 255; }"));
    EXPECT_EQ(255, exit_status);
}

// main(argc, argv) through the runtime's weak __main_void, with no arguments.
TEST_F(WasmTest, RunMainArgcArgv)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm("int main(int argc, char **argv) { return argc; }"));
    EXPECT_EQ(0, exit_status);
}

// crt0-status prints main's result in decimal, a negative one with its sign.
TEST_F(WasmTest, RunPrintStatusNegative)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("-2147483648\n", CompileAndRunBook("int main(void) { return -2147483647 - 1; }"));
    EXPECT_EQ(0, exit_status);
}

TEST_F(WasmTest, RunPrintStatusDigits)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("4294967\n", CompileAndRunBook("int main(void) { return 4294967; }"));
    EXPECT_EQ(4294967 & 255, exit_status);
}

// A hand-written module part: putch reaches the host's output.
TEST_F(WasmTest, RunPutch)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("Hi", RunAssembly("\t.functype\tputch (i32) -> ()\n"
                                "\t.section\t.text.__original_main,\"\",@\n"
                                "\t.globl\t__main_void\n"
                                "\t.type\t__main_void,@function\n"
                                "__main_void:\n"
                                "\t.functype\t__main_void () -> (i32)\n"
                                "\ti32.const\t72\n"
                                "\tcall\tputch\n"
                                "\ti32.const\t105\n"
                                "\tcall\tputch\n"
                                "\ti32.const\t0\n"
                                "\tend_function\n"));
    EXPECT_EQ(0, exit_status);
}
