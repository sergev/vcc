// The Msp430 fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): MSP430, on mspsim.
#pragma once

#include "msp430_test.h"

class CoroTest : public Msp430Test {
protected:
    void SetUp() override
    {
        Msp430Test::SetUp();
        SKIP_IF_NO_MSP430_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToMsp430(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunMsp430(src); }
};
