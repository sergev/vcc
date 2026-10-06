// The MMIX fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on mmix, each also built wholly by GCC with
// newlib, and the outputs and statuses compared.  MMIX has the book's 32-bit int, so the
// book's own expectations stand too.  The chapters are enabled in CMakeLists.txt as the
// code generator reaches them.
#pragma once

#include "mmix_test.h"

class BookTest : public MmixTest {
protected:
    void SetUp() override
    {
        MmixTest::SetUp();
        // GCC's build gives the same as ours on each; the book's expectation does not
        // hold here.  Big-endian, signed-char versions follow in K18.
        static const SkippedTest skipped[] = {
            { "Chapter16_StaticInitializers", "expects an unsigned plain char" },
            { "Chapter16_AccessThroughCharPointer", "reads an int's bytes little-endian" },
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_MMIX_TOOLS();
    }

    // Run a book program, and check that GCC -O0 with newlib gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = MmixTest::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_NE("ERROR", ours) << "did not run";
        EXPECT_EQ(GccRunBook(src), ours) << "differs from GCC";
        EXPECT_EQ(exit_status, status) << "exit status differs from GCC";
        exit_status = status;
        return ours;
    }
};
