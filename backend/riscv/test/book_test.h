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
        static const std::set<int> chapters = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        const char *name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        int chapter      = strncmp(name, "Chapter", 7) == 0 ? atoi(name + 7) : 0;
        if (!chapters.count(chapter))
            GTEST_SKIP() << "chapter " << chapter << " not yet supported by the RISC-V backend";
        // Programs RISC-V cannot run; none so far.
        static const SkippedTest skipped[] = { { nullptr, nullptr } };
        SkipIfListed(skipped);
        SKIP_IF_NO_RISCV_TOOLS();
    }
};
