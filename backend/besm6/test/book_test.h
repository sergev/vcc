// The BESM-6 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run through the Unix path under b6sim.
#pragma once

#include "codegen_test.h"

class BookTest : public CodegenTest {
protected:
    void SetUp() override
    {
        CodegenTest::SetUp();
        // Programs BESM-6 cannot run; none so far.
        static const SkippedTest skipped[] = { { nullptr, nullptr } };
        SkipIfListed(skipped);
    }
};
