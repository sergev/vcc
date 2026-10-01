// The RISC-V fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu.
#pragma once

#include "riscv_test.h"

class BookTest : public RiscvTest {
protected:
    void SetUp() override
    {
        RiscvTest::SetUp();
        SKIP_IF_NO_RISCV_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = RiscvTest::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
