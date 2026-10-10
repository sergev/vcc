// The Riscv fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): RISC-V, on bare-metal qemu (riscv64, or riscv32
// built with RISCV_TEST_XLEN=32).
#pragma once

#include "riscv_test.h"

class CoroTest : public RiscvTest {
protected:
    void SetUp() override
    {
        RiscvTest::SetUp();
        SKIP_IF_NO_RISCV_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToRiscv(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunRiscv(src); }
};
