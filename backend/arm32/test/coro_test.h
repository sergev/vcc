// The Arm32 fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): ARM32, on bare-metal qemu.
#pragma once

#include "arm32_test.h"

class CoroTest : public Arm32Test {
protected:
    void SetUp() override
    {
        Arm32Test::SetUp();
        SKIP_IF_NO_ARM32_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToArm32(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunArm32(src); }
};
