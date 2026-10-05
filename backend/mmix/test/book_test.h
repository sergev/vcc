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
        static const SkippedTest skipped[] = {
            { "Chapter13_StandardLibraryCall", "needs the C library (K14)" },
            { "Chapter13_DoubleParamsAndResultLibrary", "needs the C library (K14)" },
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
