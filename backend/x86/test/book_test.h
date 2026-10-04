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
        // Programs that call into the C library, which joins libc.a once the code
        // generator compiles it (Plan.md, X15).
#define LIBC "needs the C library in libc.a"
        static const SkippedTest skipped[] = {
            { "Chapter13_StandardLibraryCall", LIBC },
            { "Chapter13_DoubleParamsAndResultLibrary", LIBC },
            { nullptr, nullptr },
        };
#undef LIBC
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
