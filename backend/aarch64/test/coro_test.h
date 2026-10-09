// The AArch64 fixture for the shared coroutine run tests
// (backend/common/test/coro/coro_run_tests.cpp): on bare-metal qemu, or built with
// AARCH64_DARWIN natively on macOS, where the runtime is libvcc.a.
#pragma once

#include "aarch64_test.h"

class CoroTest : public Aarch64Test {
protected:
    void SetUp() override
    {
        Aarch64Test::SetUp();
        SKIP_IF_NO_AARCH64_TOOLS();
#ifdef AARCH64_DARWIN
        Config().extra_libs.push_back(AARCH64_DARWIN_LIB_DIR "/libvcc.a");
#endif
    }

    std::string Compile(const std::string &src) { return CompileToAarch64(src.c_str()); }
    std::string CompileAndRunCoro(const std::string &src) { return CompileAndRunAarch64(src); }
};
