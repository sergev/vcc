// The Avr fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): AVR, on qemu arduino-mega.
#pragma once

#include "avr_test.h"

class CoroTest : public AvrTest {
protected:
    void SetUp() override
    {
        AvrTest::SetUp();
        SKIP_IF_NO_AVR_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToAvr(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunAvr(src); }
};
