// The X86 fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): x86-64, on qemu microvm.
#pragma once

#include "x86_test.h"

class CoroTest : public X86Test {
protected:
    void SetUp() override
    {
        X86Test::SetUp();
        SKIP_IF_NO_X86_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToX86(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunX86(src); }
};
