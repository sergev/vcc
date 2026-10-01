// The RISC-V fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu.
#pragma once

#include <cstdlib>
#include <cstring>
#include <set>

#include "riscv_test.h"

class BookTest : public RiscvTest {
protected:
    void SetUp() override
    {
        RiscvTest::SetUp();
        // Chapters the backend handles so far; the list grows with Phase 3.
        static const std::set<int> chapters = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
        const char *name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        int chapter      = strncmp(name, "Chapter", 7) == 0 ? atoi(name + 7) : 0;
        if (!chapters.count(chapter))
            GTEST_SKIP() << "chapter " << chapter << " not yet supported by the RISC-V backend";
        // Programs whose expected values are BESM-6's (41-bit int, 48-bit unsigned; on
        // RV64 each gives what clang gives), and programs needing a missing library.
#define W "expects BESM-6 integer widths"
        static const SkippedTest skipped[] = {
            { "Chapter11_Bitshift", W },
            { "Chapter11_CompoundAssignToInt", W },
            { "Chapter11_CompoundBitshift", W },
            { "Chapter11_ConvertByAssignment", W },
            { "Chapter11_ConvertFunctionArguments", W },
            { "Chapter11_ConvertStaticInitializer", W },
            { "Chapter11_LongGlobalVar", W },
            { "Chapter11_SwitchInt", W },
            { "Chapter11_Truncate", W },
            { "Chapter12_ArithmeticWraparound", W },
            { "Chapter12_BitwiseUnsignedShift", W },
            { "Chapter12_ChainedCasts", W },
            { "Chapter12_CommonType", W },
            { "Chapter12_CompoundAssignUint", W },
            { "Chapter12_CompoundBitshift", W },
            { "Chapter12_CompoundBitwise", W },
            { "Chapter12_ConvertByAssignment", W },
            { "Chapter12_Extension", W },
            { "Chapter12_Locals", W },
            { "Chapter12_PostfixPrecedence", W },
            { "Chapter12_RoundTripCasts", W },
            { "Chapter12_SameSizeConversion", W },
            { "Chapter12_StaticInitializers", W },
            { "Chapter12_UnsignedIncrDecr", W },
            { "Chapter13_StandardLibraryCall", "needs fma and ldexp (R17)" },
            { "Chapter13_DoubleParamsAndResultLibrary", "needs fmax (R17)" },
            { nullptr, nullptr },
        };
#undef W
        SkipIfListed(skipped);
        SKIP_IF_NO_RISCV_TOOLS();
    }
};
