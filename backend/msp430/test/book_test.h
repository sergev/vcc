// The MSP430 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on mspsim, each also compiled by clang and
// the two outputs compared.  The book's expected values assume a 32-bit int, clang's
// MSP430 output does not, so the comparison is what makes a 16-bit int testable.  The
// chapters are enabled in CMakeLists.txt as the code generator reaches them.
#pragma once

#include "msp430_test.h"

class BookTest : public Msp430Test {
protected:
    void SetUp() override
    {
        Msp430Test::SetUp();
        static const SkippedTest skipped[] = {
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_MSP430_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = Msp430Test::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_NE("ERROR", ours) << "did not run";
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
