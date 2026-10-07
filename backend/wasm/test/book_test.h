// The wasm32 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run under node, each also compiled by clang and
// the two outputs compared.
#pragma once

#include "wasm_test.h"

class BookTest : public WasmTest {
protected:
    void SetUp() override
    {
        WasmTest::SetUp();
        // Programs whose expected results assume a 64-bit long, as on ARM32: each gives
        // what clang gives, and three are not valid C on ILP32 at all.
#define L "expects a 64-bit long"
        static const SkippedTest skipped[] = {
            { "Chapter11_ArithmeticOps", L },
            { "Chapter11_Assign", L },
            { "Chapter11_Bitshift", L },
            { "Chapter11_BitwiseLongOp", L },
            { "Chapter19_WP_AllTypes_FoldCompoundAssignAllTypes", L },
            { "Chapter19_WP_AllTypes_FoldCompoundBitwiseAssignAllTypes", L },
            { "Chapter19_WP_AllTypes_FoldExtensionAndTruncation", L },
            { "Chapter19_WP_AllTypes_FoldNegativeValues", L },
            { "Chapter19_WP_AllTypes_SignedUnsignedConversion", L },
            { nullptr, nullptr },
        };
#undef L
        SkipIfListed(skipped);
        SKIP_IF_NO_WASM32_TOOLS();
    }

    // Run a book program, and check that clang -O0 gives the same, when present.
    // cppcheck-suppress duplInheritedMember ; hides the base version on purpose, and calls it
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string ours = WasmTest::CompileAndRunBook(src);
        int status       = exit_status;
        // cppcheck-suppress knownConditionTrueFalse ; depends on the configured toolchain
        if (wasm32_clang_available()) {
            EXPECT_EQ(ClangRunBook(src), ours) << "differs from clang";
            EXPECT_EQ(exit_status, status) << "exit status differs from clang";
            exit_status = status;
        }
        return ours;
    }
};
