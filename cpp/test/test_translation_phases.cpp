// C11 §5.1.1.2 — Translation phases (as observable through the preprocessor).
// Phase 2: backslash-newline splicing.  Phase 3: comments become one space.
#include <sstream>

#include "test_support.h"

using TranslationPhases = PreprocessorTest;

// Phase 2: a backslash-newline in a macro replacement list is spliced away.
TEST_F(TranslationPhases, LineSpliceInReplacementList)
{
    EXPECT_TOKENS(
        "#define A 1+\\\n2\n"
        "A\n",
        "1+2");
}

// Phase 2: splicing occurs before tokenization, so it can join a single token.
TEST_F(TranslationPhases, LineSpliceJoinsToken)
{
    EXPECT_TOKENS(
        "#define CAT foo\\\nbar\n"
        "CAT\n",
        "foobar");
}

// Phase 3: a block comment is replaced by a single space (it does not vanish).
TEST_F(TranslationPhases, BlockCommentBecomesSpace)
{
    EXPECT_TOKENS("a/**/b\n", "a b");
}

// Phase 3: a // comment runs to end of line and is removed.
TEST_F(TranslationPhases, LineCommentRemoved)
{
    EXPECT_TOKENS(
        "a//trailing\n"
        "b\n",
        "a\nb");
}

// Phase 3: a block comment may span multiple physical lines.
TEST_F(TranslationPhases, BlockCommentSpansLines)
{
    EXPECT_TOKENS(
        "x/* one\n"
        "two */y\n",
        "x y");
}

// A comment that is never closed cannot be tokenized: a diagnostic is required.
TEST_F(TranslationPhases, UnterminatedCommentDiagnosed)
{
    EXPECT_PP_DIAGNOSES("a /* never closed\n");
}

// Phase 2: the newline a splice removes is restored after the joined line, so
// the text that follows keeps its source line number -- in ordinary text, after
// a #define and after a #if.  Line markers are dropped before counting.
TEST_F(TranslationPhases, LineSpliceKeepsLineNumbers)
{
    Result r = Preprocess(R"(int a = 1 + \
  2;
int b;
#define X 1 + \
  2
int c = X;
#if 1 + \
  1
int d;
#endif
int e;
)");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    std::vector<std::string> lines;
    std::istringstream in(r.out);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("#", 0) != 0)
            lines.push_back(line);
    }
    ASSERT_GE(lines.size(), 11u) << r.out;
    EXPECT_EQ(lines[2], "int b;");
    EXPECT_EQ(lines[5].find("int c"), 0u);
    EXPECT_EQ(lines[8], "int d;");
    EXPECT_EQ(lines[10], "int e;");
}
