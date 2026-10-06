// The MSP430 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on mspsim, each also built wholly by GCC with
// newlib, and by clang on our runtime, and the outputs compared.  The book's expected values assume a 32-bit int
// and a 64-bit long, GCC's MSP430 output does not: GCC is the oracle here, and the book's
// own expectation is set aside (its failures are intercepted and dropped).  The chapters
// are enabled in CMakeLists.txt as the code generator reaches them.
#pragma once

#include <gtest/gtest-spi.h>

#include <cstring>
#include <memory>

#include "msp430_test.h"

class BookTest : public Msp430Test {
    ::testing::TestPartResultArray results;
    std::unique_ptr<::testing::ScopedFakeTestPartResultReporter> intercept;

protected:
    void SetUp() override
    {
        Msp430Test::SetUp();
        // Each fails in GCC's build too, or is undefined and GCC differs.
        static const SkippedTest skipped[] = {
            { "Chapter3_BitwiseShiftrNegative", "shifts an int by 30: undefined (GCC -1)" },
            { "Chapter11_SwitchLong", "case values collide in a 32-bit long, as GCC says" },
            { "Chapter12_UnsignedTypeSpecifiers",
              "loops forever with a 16-bit unsigned, GCC's too" },
            { "Chapter13_DoubleAndIntParamsRecursive", "past the cycle limit, GCC's too" },
            { "Chapter13_DoubleAndIntParamsRecursiveLibrary", "past the cycle limit, GCC's too" },
            { "Chapter14_SwitchDereferencedPointer",
              "case values collide in a 32-bit long, as GCC says" },
            { "Chapter15_BigArray", "arrays too large for a 16-bit size_t, as GCC says" },
            { "Chapter16_AccessThroughCharPointer",
              "reads past a 16-bit int and past an array: the stack's garbage" },
            { "Chapter16_StandardLibraryCalls",
              "declares strlen unsigned long: its high word is r13's garbage" },
            { "Chapter16_StringsInFunctionCalls",
              "declares strlen unsigned long: its high word is r13's garbage" },
            { "Chapter17_SizeofExtern", "arrays too large for 15.5 KB of RAM, as GCC says" },
            { nullptr, nullptr },
        };
        SkipIfListed(skipped);
        SKIP_IF_NO_MSP430_TOOLS();
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
        Msp430Test::TearDown();
    }

    // The programs whose behaviour C leaves undefined at 16 bits, where GCC and clang
    // differ: ours must match GCC's, and is not compared with clang's.
    static bool ClangDiffers()
    {
        static const SkippedTest differs[] = {
            { "Chapter16_CompoundBitwiseOpsChars", "shifts an int by 31" },
            { "Chapter19_WP_AllTypes_FoldCompoundBitwiseAssignAllTypes", "shifts an int by 31" },
            { nullptr, nullptr },
        };
        const char *name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        for (const SkippedTest *d = differs; d->name; d++)
            if (strcmp(d->name, name) == 0)
                return true;
        return false;
    }

    // Run a book program, and check that GCC -O0 with newlib gives the same, and clang
    // -O0 on our runtime too, where each is present.
    // cppcheck-suppress duplInheritedMember ; deliberately wraps Msp430Test's version
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = Msp430Test::CompileAndRunBook(src);
        int status       = exit_status;
        EXPECT_NE("ERROR", ours) << "did not run";
        // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
        if (msp430_gcc_available()) {
            EXPECT_EQ(GccRunBook(src), ours) << "differs from GCC";
            EXPECT_EQ(exit_status, status) << "exit status differs from GCC";
        }
        // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
        if (msp430_clang_available() && !ClangDiffers()) {
            std::string clang = ClangRunBook(src);
            EXPECT_EQ(clang, ours) << "differs from clang";
            EXPECT_EQ(exit_status, status) << "exit status differs from clang";
        }
        exit_status = status;
        return ours;
    }
};
