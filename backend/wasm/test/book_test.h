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
        // Programs whose expected results assume a 64-bit long, as on ARM32 (each gives
        // what clang gives, and three are not valid C on ILP32 at all), and those that
        // assume an unsigned plain char beyond them: signed_char_tests.cpp has versions
        // of those.
#define L "expects a 64-bit long"
        static const SkippedTest skipped[] = {
            { "Chapter11_ArithmeticOps", L },
            { "Chapter11_Assign", L },
            { "Chapter11_Bitshift", L },
            { "Chapter11_BitwiseLongOp", L },
            { "Chapter11_CommonType", L },
            { "Chapter11_Comparisons", L },
            { "Chapter11_CompoundAssignToLong", L },
            { "Chapter11_CompoundBitshift", L },
            { "Chapter11_CompoundBitwise", L },
            { "Chapter11_IncrementLong", L },
            { "Chapter11_LargeConstants", L },
            { "Chapter11_Logical", L },
            { "Chapter11_LongAndIntLocals", L },
            { "Chapter11_LongArgs", L },
            { "Chapter11_LongArgsLibrary", L },
            { "Chapter11_LongConstants", L },
            { "Chapter11_LongGlobalVar", L },
            { "Chapter11_MultiOp", L },
            { "Chapter11_ReturnLong", L },
            { "Chapter11_ReturnLongLibrary", L },
            { "Chapter11_RewriteLargeMultiplyRegression", L },
            { "Chapter11_Simple", L },
            { "Chapter11_StaticLong", L },
            { "Chapter11_SwitchLong", "duplicate case values when long has 32 bits" },
            { "Chapter11_TypeSpecifiers", L },
            { "Chapter12_ArithmeticOps", L },
            { "Chapter12_ArithmeticWraparound", L },
            { "Chapter12_BitwiseUnsignedOps", L },
            { "Chapter12_ChainedCasts", L },
            { "Chapter12_CommonType", L },
            { "Chapter12_CompoundAssignUint", L },
            { "Chapter12_CompoundBitshift", L },
            { "Chapter12_CompoundBitwise", L },
            { "Chapter12_ConvertByAssignment", L },
            { "Chapter12_Locals", L },
            { "Chapter12_PromoteConstants", L },
            { "Chapter12_RoundTripCasts", L },
            { "Chapter12_StaticInitializers", L },
            { "Chapter12_StaticVariables", L },
            { "Chapter12_UnsignedArgsLibrary", L },
            { "Chapter12_UnsignedIncrDecr", L },
            { "Chapter13_CompoundAssignImplicitCast", L },
            { "Chapter13_DoubleToSigned", L },
            { "Chapter13_DoubleToUnsigned", L },
            { "Chapter13_SignedToDouble", L },
            { "Chapter13_StaticInitializers", L },
            { "Chapter13_UnsignedToDouble", L },
            { "Chapter14_BitwiseOpsWithDereferencedPtrs", L },
            { "Chapter14_CompoundAssignConversion", L },
            { "Chapter14_CompoundBitwiseDereferencedPtrs", L },
            { "Chapter14_IncrAndDecrThroughPointer", L },
            { "Chapter14_ReadThroughPointers", L },
            { "Chapter14_SwitchDereferencedPointer",
              "duplicate case values when long has 32 bits" },
            { "Chapter15_Automatic", L },
            { "Chapter15_BigArray", "array too large when long has 32 bits" },
            { "Chapter15_CompoundBitwiseSubscript", L },
            { "Chapter15_ImplicitAndExplicitConversions", L },
            { "Chapter16_BitwiseOpsCharacterConstants", L },
            { "Chapter16_StaticInitializers", "expects an unsigned plain char" },
            { "Chapter16_CommonType", L },
            { "Chapter16_ConvertByAssignment", L },
            { "Chapter17_SizeofArray", L },
            { "Chapter17_SizeofBasicTypes", L },
            { "Chapter17_SizeofBitwise", L },
            { "Chapter17_SizeofCompound", L },
            { "Chapter17_SizeofCompoundBitwise", L },
            { "Chapter17_SizeofConsts", L },
            { "Chapter17_SizeofDerivedTypes", L },
            { "Chapter17_SizeofExpressions", L },
            { "Chapter17_SizeofIncr", L },
            { "Chapter17_SizeofResultIsUlong", L },
            { "Chapter18_BitwiseOpsStructMembers", L },
            { "Chapter18_ClassifyParams", L },
            { "Chapter18_CompoundAssignStructMembers", L },
            { "Chapter18_CopyThruPointer", L },
            { "Chapter18_IncrStructMembers", L },
            { "Chapter18_NestedStaticStructInitializers", L },
            { "Chapter18_ParamCallingConventions", L },
            { "Chapter18_ScalarMemberAccessArrow", L },
            { "Chapter18_ScalarMemberAccessDot", L },
            { "Chapter18_StaticUnionAccess", L },
            { "Chapter18_UnionInitAndMemberAccess", L },
            { "Chapter18_UnionInits", L },
            { "Chapter18_UnionRetvals", L },
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
