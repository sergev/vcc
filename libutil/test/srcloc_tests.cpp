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
        diag_color       = 0;
    }

    void TearDown() override
    {
        unlink(path.c_str());
        diag_color = -1;
    }

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

// With color, the caret is bold green; the source line stays plain.
TEST_F(SourceExcerpt, ColoredCaret)
{
    diag_color = 1;
    EXPECT_EQ(Excerpt(1, 5), "    1 | int a;\n      |     \033[1;32m^\033[0m\n");
}

// What diag_print_prefix() prints for a location and kind.
static std::string Prefix(SrcLoc loc, const char *kind)
{
    FILE *f = tmpfile();
    if (!f) {
        ADD_FAILURE() << "tmpfile";
        return "";
    }
    diag_print_prefix(f, loc, kind);
    std::string out;
    rewind(f);
    for (int c; (c = getc(f)) != EOF;) {
        out += (char)c;
    }
    fclose(f);
    return out;
}

class DiagPrefix : public ::testing::Test {
protected:
    void TearDown() override
    {
        diag_color    = -1;
        diag_progname = nullptr;
    }
};

TEST_F(DiagPrefix, Plain)
{
    diag_color = 0;
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 3, 7 }, "error"), "t.c:3:7: error: ");
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 3, 0 }, "warning"), "t.c:3: warning: ");
    EXPECT_EQ(Prefix(SrcLoc{}, "note"), "note: ");
    diag_progname = "/usr/bin/vlower";
    EXPECT_EQ(Prefix(SrcLoc{}, "error"), "vlower: error: ");
}

// The location is bold; each kind has its own color.
TEST_F(DiagPrefix, Colored)
{
    diag_color = 1;
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 3, 7 }, "error"),
              "\033[1mt.c:3:7:\033[0m \033[1;31merror:\033[0m ");
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 3, 7 }, "warning"),
              "\033[1mt.c:3:7:\033[0m \033[1;35mwarning:\033[0m ");
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 3, 7 }, "note"),
              "\033[1mt.c:3:7:\033[0m \033[1;36mnote:\033[0m ");
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 3, 7 }, "internal compiler error"),
              "\033[1mt.c:3:7:\033[0m \033[1;31minternal compiler error:\033[0m ");
    EXPECT_EQ(Prefix(SrcLoc{}, "error"), "\033[1;31merror:\033[0m ");
    diag_progname = "vlower";
    EXPECT_EQ(Prefix(SrcLoc{}, "error"), "\033[1mvlower:\033[0m \033[1;31merror:\033[0m ");
}

// By default, color only goes to a terminal; a temporary file is not one.
TEST_F(DiagPrefix, AutoIsPlainForAFile)
{
    diag_color = -1;
    EXPECT_EQ(Prefix(SrcLoc{ "t.c", 1, 1 }, "error"), "t.c:1:1: error: ");
}

TEST_F(DiagPrefix, SetColor)
{
    EXPECT_EQ(diag_set_color("always"), 0);
    EXPECT_EQ(diag_color, 1);
    EXPECT_EQ(diag_set_color("never"), 0);
    EXPECT_EQ(diag_color, 0);
    EXPECT_EQ(diag_set_color("auto"), 0);
    EXPECT_EQ(diag_color, -1);
    EXPECT_EQ(diag_set_color("maybe"), -1);
    EXPECT_EQ(diag_color, -1);
}
