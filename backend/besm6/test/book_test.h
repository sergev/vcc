// The BESM-6 fixture for the shared "Writing a C Compiler" suite
// (backend/common/test/book/): programs run through the Unix path under b6sim.
#pragma once

#include "codegen_test.h"

class BookTest : public CodegenTest {
protected:
    void SetUp() override
    {
        CodegenTest::SetUp();
        // Generic programs whose results depend on integer widths or sizes; their
        // BESM-6 versions are in book_besm6_tests.cpp.
#define G "BESM-6 version in book_besm6_tests.cpp"
        static const SkippedTest skipped[] = {
            { "Chapter11_Bitshift", G },
            { "Chapter11_CompoundAssignToInt", G },
            { "Chapter11_CompoundBitshift", G },
            { "Chapter11_ConvertByAssignment", G },
            { "Chapter11_ConvertFunctionArguments", G },
            { "Chapter11_ConvertStaticInitializer", G },
            { "Chapter11_LongGlobalVar", G },
            { "Chapter11_SwitchInt", G },
            { "Chapter11_Truncate", G },
            { "Chapter12_ArithmeticWraparound", G },
            { "Chapter12_BitwiseUnsignedShift", G },
            { "Chapter12_ChainedCasts", G },
            { "Chapter12_CommonType", G },
            { "Chapter12_CompoundAssignUint", G },
            { "Chapter12_CompoundBitshift", G },
            { "Chapter12_CompoundBitwise", G },
            { "Chapter12_ConvertByAssignment", G },
            { "Chapter12_Extension", G },
            { "Chapter12_Locals", G },
            { "Chapter12_PostfixPrecedence", G },
            { "Chapter12_RoundTripCasts", G },
            { "Chapter12_SameSizeConversion", G },
            { "Chapter12_StaticInitializers", G },
            { "Chapter12_UnsignedIncrDecr", G },
            { "Chapter14_BitshiftDereferencedPtrs", G },
            { "Chapter14_CompoundBitwiseDereferencedPtrs", G },
            { "Chapter14_IncrAndDecrThroughPointer", G },
            { "Chapter15_Automatic", G },
            { "Chapter15_CompoundAssignToSubscriptedVal", G },
            { "Chapter15_CompoundBitwiseSubscript", G },
            { "Chapter15_CompoundPointerAssignment", G },
            { "Chapter15_ImplicitAndExplicitConversions", G },
            { "Chapter16_AccessThroughCharPointer", G },
            { "Chapter16_BitshiftChars", G },
            { "Chapter16_BitwiseOpsChars", G },
            { "Chapter16_CommonType", G },
            { "Chapter16_ConvertByAssignment", G },
            { "Chapter16_ExplicitCasts", G },
            { "Chapter17_SizeofArray", G },
            { "Chapter17_SizeofBasicTypes", G },
            { "Chapter17_SizeofBitwise", G },
            { "Chapter17_SizeofCompound", G },
            { "Chapter17_SizeofCompoundBitwise", G },
            { "Chapter17_SizeofConsts", G },
            { "Chapter17_SizeofDerivedTypes", G },
            { "Chapter17_SizeofExpressions", G },
            { "Chapter17_SizeofExtern", G },
            { "Chapter17_SizeofIncr", G },
            { "Chapter17_SizeofNotEvaluated", G },
            { "Chapter17_SizeofResultIsUlong", G },
            { "Chapter17_SizeofSimple", G },
            { "Chapter18_CopyThruPointer", G },
            { "Chapter18_IncrStructMembers", G },
            { "Chapter18_MemberOffsets", G },
            { "Chapter18_NestedStaticStructInitializers", G },
            { "Chapter18_NestedUnionAccess", G },
            { "Chapter18_SizeofExps", G },
            { "Chapter18_SizeofType", G },
            { "Chapter18_StaticStructInitializers", G },
            { "Chapter18_StaticUnionAccess", G },
            { "Chapter18_StaticUnionInits", G },
            { "Chapter18_UnionInitAndMemberAccess", G },
            { "Chapter18_UnionSizes", G },
            { "Chapter18_UnionTempLifetime", G },
            { "Chapter18_UnionsInConditionals", G },
            { "Chapter19_WP_AllTypes_FoldCompoundAssignAllTypes", G },
            { "Chapter19_WP_AllTypes_FoldExtensionAndTruncation", G },
            { "Chapter19_WP_AllTypes_FoldIncrDecrUnsigned", G },
            { "Chapter19_WP_AllTypes_FoldNegativeLongBitshift", G },
            { "Chapter19_WP_AllTypes_SignedUnsignedConversion", G },
            { "Chapter19_WP_IntOnly_FoldNegativeBitshift", G },
            { "Chapter20_AllNoCoal_TypeConversionInterference", G },
            { nullptr, nullptr },
        };
#undef G
        SkipIfListed(skipped);
    }
};
