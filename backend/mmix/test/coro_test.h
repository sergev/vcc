// The Mmix fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): MMIX, on Knuth's mmix.
#pragma once

#include "mmix_test.h"

class CoroTest : public MmixTest {
protected:
    void SetUp() override
    {
        MmixTest::SetUp();
        SKIP_IF_NO_MMIX_TOOLS();
    }

    std::string Compile(const std::string &src) { return CompileToMmix(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunMmix(src); }
};
