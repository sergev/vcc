// The AVR fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu, each also compiled by
// clang and the two outputs compared.  The book's expected values assume a 32-bit int
// and a 64-bit long, clang's AVR output does not: clang is the oracle here, and the
// book's own expectation is set aside (its failures are intercepted and dropped).
// The chapters are enabled in CMakeLists.txt as the code generator reaches them.
#pragma once

#include <gtest/gtest-spi.h>

#include <cstring>
#include <memory>

#include "avr_test.h"

class BookTest : public AvrTest {
    ::testing::TestPartResultArray results;
    std::unique_ptr<::testing::ScopedFakeTestPartResultReporter> intercept;

protected:
    void SetUp() override
    {
        AvrTest::SetUp();
        static const SkippedTest skipped[] = {
            { "Chapter3_BitwiseShiftPrecedence", "shifts a 16-bit int by 16: undefined" },
            { "Chapter3_BitwiseShiftrNegative", "shifts a 16-bit int by 30: undefined" },
            { "Chapter11_LargeConstants", "clang -O0 miscompiles a long long compare" },
            { "Chapter11_SwitchLong", "case values collide in a 32-bit long" },
            { "Chapter12_UnsignedTypeSpecifiers", "loops forever with a 16-bit unsigned" },
            { "Chapter13_DoubleAndIntParamsRecursive", "two minutes under qemu, clang's too" },
            { "Chapter13_DoubleAndIntParamsRecursiveLibrary",
              "two minutes under qemu, clang's too" },
            { "Chapter14_SwitchDereferencedPointer", "case values collide in a 32-bit long" },
            { "Chapter15_BigArray", "arrays too large for a 16-bit size_t" },
            { "Chapter15_ArrayOfPointersToArrays", "clang -O0 runs out of registers" },
            { "Chapter16_AccessThroughCharPointer", "reads past a 16-bit int" },
            { "Chapter16_CompoundBitwiseOpsChars", "shifts an int by 31: undefined" },
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_AVR_TOOLS();
        intercept.reset(new ::testing::ScopedFakeTestPartResultReporter(
            ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD,
            &results));
    }

    // Report again every result but the failures of the book's own expectations.
    void TearDown() override
    {
        intercept.reset();
        for (int i = 0; i < results.size(); i++) {
            const ::testing::TestPartResult &r = results.GetTestPartResult(i);
            const char *file                   = r.file_name() ? r.file_name() : "";
            if (r.skipped())
                GTEST_SKIP() << r.message();
            else if (r.failed() && !strstr(file, "/test/book/chapter"))
                ADD_FAILURE_AT(file, r.line_number()) << r.message();
        }
        AvrTest::TearDown();
    }

    // Run a book program, and check that clang -O0 gives the same.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = AvrTest::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_NE("ERROR", ours) << "did not run";
        EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
        EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        exit_status = status;
        return ours;
    }
};
