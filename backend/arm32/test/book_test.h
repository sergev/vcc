// The ARM32 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu, each also compiled by
// clang and the two outputs compared.  The chapters are enabled in CMakeLists.txt as
// the code generator reaches them.
#pragma once

#include "arm32_test.h"

class BookTest : public Arm32Test {
protected:
    void SetUp() override
    {
        Arm32Test::SetUp();
        static const SkippedTest skipped[] = {
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_ARM32_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = Arm32Test::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
