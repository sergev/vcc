// The x86-64 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu, each also compiled by
// clang and the two outputs compared.  The chapters are enabled in CMakeLists.txt as
// the code generator reaches them.
#pragma once

#include "x86_test.h"

class BookTest : public X86Test {
protected:
    void SetUp() override
    {
        X86Test::SetUp();
        // Plain char is signed here, as clang has it; the other byte-addressed targets
        // have it unsigned.
        static const SkippedTest skipped[] = {
            { "Chapter16_StaticInitializers", "expects an unsigned plain char" },
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_X86_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = X86Test::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
