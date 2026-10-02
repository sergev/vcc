// The AArch64 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu, each also compiled by
// clang and the two outputs compared.  The chapters are enabled in CMakeLists.txt as
// the code generator reaches them.
#pragma once

#include "aarch64_test.h"

class BookTest : public Aarch64Test {
protected:
    void SetUp() override
    {
        Aarch64Test::SetUp();
        static const SkippedTest skipped[] = {
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_AARCH64_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = Aarch64Test::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
