// The AVR fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu, each also compiled by
// clang and the two outputs compared.  The book's expected values assume a 32-bit int,
// clang's AVR output does not, so the comparison is what makes a 16-bit int testable.
// The chapters are enabled in CMakeLists.txt as the code generator reaches them.
#pragma once

#include "avr_test.h"

class BookTest : public AvrTest {
protected:
    void SetUp() override
    {
        AvrTest::SetUp();
        static const SkippedTest skipped[] = {
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_AVR_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = AvrTest::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
