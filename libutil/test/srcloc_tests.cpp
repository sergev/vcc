//
// Tests for the source line and caret under a diagnostic.
//
#include <gtest/gtest.h>
#include <stdio.h>
#include <unistd.h>

#include <string>

#include "srcloc.h"

class SourceExcerpt : public ::testing::Test {
protected:
    std::string path;

    void SetUp() override
    {
        char name[] = "/tmp/srclocXXXXXX";
        int fd      = mkstemp(name);
        ASSERT_GE(fd, 0);
        const char text[] = "int a;\n\tint b = 1x;\r\nlast";
        ASSERT_EQ(write(fd, text, sizeof text - 1), (ssize_t)(sizeof text - 1));
        close(fd);
        path             = name;
        diag_show_source = 1;
    }

    void TearDown() override { unlink(path.c_str()); }

    // What diag_print_source() prints for the line and column.
    std::string Excerpt(int line, int col, const char *file = nullptr)
    {
        FILE *f = tmpfile();
        if (!f) {
            ADD_FAILURE() << "tmpfile";
            return "";
        }
        diag_print_source(f, SrcLoc{ file ? file : path.c_str(), line, col });
        std::string out;
        rewind(f);
        for (int c; (c = getc(f)) != EOF;) {
            out += (char)c;
        }
        fclose(f);
        return out;
    }
};

TEST_F(SourceExcerpt, LineAndCaret)
{
    EXPECT_EQ(Excerpt(1, 5), "    1 | int a;\n      |     ^\n");
}

// A tab before the column is kept, and a carriage return is dropped.
TEST_F(SourceExcerpt, TabAndCarriageReturn)
{
    EXPECT_EQ(Excerpt(2, 10), "    2 | \tint b = 1x;\n      | \t        ^\n");
}

// The last line needs no newline; the caret may stand just past its end.
TEST_F(SourceExcerpt, LastLine)
{
    EXPECT_EQ(Excerpt(3, 5), "    3 | last\n      |     ^\n");
}

// Without a column, or with one past the end of the line, the caret is left out.
TEST_F(SourceExcerpt, NoCaret)
{
    EXPECT_EQ(Excerpt(1, 0), "    1 | int a;\n");
    EXPECT_EQ(Excerpt(1, 9), "    1 | int a;\n");
}

// Nothing for a line past the end, an unknown line, a missing file, or when off.
TEST_F(SourceExcerpt, Nothing)
{
    EXPECT_EQ(Excerpt(4, 1), "");
    EXPECT_EQ(Excerpt(0, 1), "");
    EXPECT_EQ(Excerpt(1, 1, "/nonexistent/file.c"), "");
    EXPECT_EQ(Excerpt(1, 1, ""), "");
    diag_show_source = 0;
    EXPECT_EQ(Excerpt(1, 1), "");
}
