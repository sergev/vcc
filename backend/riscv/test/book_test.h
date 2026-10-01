// The RISC-V fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run on bare-metal qemu.
#pragma once

#include "riscv_test.h"

class BookTest : public RiscvTest {
protected:
    void SetUp() override
    {
        RiscvTest::SetUp();
        // Programs whose expected values are BESM-6's (41-bit int, 48-bit unsigned,
        // 6-byte words; on RV64 each gives what clang gives), and programs needing a
        // missing library.
#define W "expects BESM-6 integer widths or sizes"
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
            { "Chapter14_BitshiftDereferencedPtrs", W },
            { "Chapter14_CompoundBitwiseDereferencedPtrs", W },
            { "Chapter14_IncrAndDecrThroughPointer", W },
            { "Chapter15_Automatic", W },
            { "Chapter15_CompoundAssignToSubscriptedVal", W },
            { "Chapter15_CompoundBitwiseSubscript", W },
            { "Chapter15_CompoundPointerAssignment", W },
            { "Chapter15_ImplicitAndExplicitConversions", W },
            { "Chapter16_AccessThroughCharPointer", W },
            { "Chapter16_BitshiftChars", W },
            { "Chapter16_BitwiseOpsChars", W },
            { "Chapter16_CommonType", W },
            { "Chapter16_ConvertByAssignment", W },
            { "Chapter16_ExplicitCasts", W },
            { "Chapter17_SizeofArray", W },
            { "Chapter17_SizeofBasicTypes", W },
            { "Chapter17_SizeofBitwise", W },
            { "Chapter17_SizeofCompound", W },
            { "Chapter17_SizeofCompoundBitwise", W },
            { "Chapter17_SizeofConsts", W },
            { "Chapter17_SizeofDerivedTypes", W },
            { "Chapter17_SizeofExpressions", W },
            { "Chapter17_SizeofExtern", W },
            { "Chapter17_SizeofIncr", W },
            { "Chapter17_SizeofNotEvaluated", W },
            { "Chapter17_SizeofResultIsUlong", W },
            { "Chapter17_SizeofSimple", W },
            { "Chapter18_CopyThruPointer", W },
            { "Chapter18_IncrStructMembers", W },
            { "Chapter18_MemberOffsets", W },
            { "Chapter18_NestedStaticStructInitializers", W },
            { "Chapter18_NestedUnionAccess", W },
            { "Chapter18_SizeofExps", W },
            { "Chapter18_SizeofType", W },
            { "Chapter18_StaticStructInitializers", W },
            { "Chapter18_StaticUnionAccess", W },
            { "Chapter18_StaticUnionInits", W },
            { "Chapter18_UnionInitAndMemberAccess", W },
            { "Chapter18_UnionSizes", W },
            { "Chapter18_UnionTempLifetime", W },
            { "Chapter18_UnionsInConditionals", W },
            { "Chapter19_WP_AllTypes_FoldCompoundAssignAllTypes", W },
            { "Chapter19_WP_AllTypes_FoldExtensionAndTruncation", W },
            { "Chapter19_WP_AllTypes_FoldIncrDecrUnsigned", W },
            { "Chapter19_WP_AllTypes_FoldNegativeLongBitshift", W },
            { "Chapter19_WP_AllTypes_SignedUnsignedConversion", W },
            { "Chapter19_WP_IntOnly_FoldNegativeBitshift", W },
            { "Chapter20_AllNoCoal_TypeConversionInterference", W },
            { "Chapter13_StandardLibraryCall", "needs fma and ldexp (R17)" },
            { "Chapter13_DoubleParamsAndResultLibrary", "needs fmax (R17)" },
            { "Chapter16_StandardLibraryCalls", "needs atoi (R17)" },
            { nullptr, nullptr },
        };
#undef W
        SkipIfListed(skipped);
        SKIP_IF_NO_RISCV_TOOLS();
    }
};
