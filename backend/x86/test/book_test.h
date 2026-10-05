// The x86-64 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu, each also compiled by
// clang and the two outputs compared.
#pragma once

#include "x86_test.h"

class BookTest : public X86Test {
protected:
    void SetUp() override
    {
        X86Test::SetUp();
        // Plain char is signed here, as clang has it; the other byte-addressed targets
        // have it unsigned.  book_x86_tests.cpp runs signed-char versions of these.
        static const SkippedTest skipped[] = {
            { "Chapter16_StaticInitializers", "expects an unsigned plain char" },
            { "Chapter18_ClassifyParams", "expects an unsigned plain char" },
            { "Chapter18_UnionInits", "expects an unsigned plain char" },
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_X86_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    // cppcheck-suppress duplInheritedMember ; deliberately wraps X86Test::CompileAndRunBook
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
