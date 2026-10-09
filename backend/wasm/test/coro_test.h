// The Wasm fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): wasm32, under node.
#pragma once

#include "wasm_test.h"

class CoroTest : public WasmTest {
protected:
    void SetUp() override
    {
        WasmTest::SetUp();
        SKIP_IF_NO_WASM32_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToWasm(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunWasm(src); }
};
