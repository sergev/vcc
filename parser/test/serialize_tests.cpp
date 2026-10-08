#include "fixture.h"

TEST_F(ParserTest, ExportEmptyProgram)
{
    program = new_program();

    int fd = CreateAstFile();
    export_ast(fd, program);

    Program *deserialized = import_ast(fd);
    EXPECT_TRUE(compare_program(program, deserialized));
    close(fd);
    free_program(deserialized);
}

TEST_F(ParserTest, ExportSimpleFunction)
{
    program = parse(CreateTempFile("int main() { return 0; }"));
    ASSERT_NE(nullptr, program);
    print_program(stdout, program);

    int fd = CreateAstFile();
    export_ast(fd, program);

    Program *deserialized = import_ast(fd);
    print_program(stdout, deserialized);
    EXPECT_TRUE(compare_program(program, deserialized));
    close(fd);
    free_program(deserialized);
}

// The last expression kind, __builtin_va_class, survives a round trip.
TEST_F(ParserTest, ExportVaClass)
{
    program = parse(CreateTempFile(R"(
int f(void) { return __builtin_va_class(double) + _Generic(1, int: 2); }
)"));
    ASSERT_NE(nullptr, program);

    int fd = CreateAstFile();
    export_ast(fd, program);

    Program *deserialized = import_ast(fd);
    EXPECT_TRUE(compare_program(program, deserialized));
    close(fd);
    free_program(deserialized);
}

// The last statement kind, _Defer, survives a round trip, in every position.
TEST_F(ParserTest, ExportDefer)
{
    program = parse(CreateTempFile(R"(
void g(int);
void f(int x) { _Defer g(1); _Defer { g(2); g(3); } if (x) _Defer g(4); }
)"));
    ASSERT_NE(nullptr, program);

    int fd = CreateAstFile();
    export_ast(fd, program);

    Program *deserialized = import_ast(fd);
    EXPECT_TRUE(compare_program(program, deserialized));
    close(fd);
    free_program(deserialized);
}

#if 0
TEST_F(ParserTest, ExportComplexType)
{
    program = parse(CreateTempFile("struct Person { char name[50]; int age; }"));
    ...
}

TEST_F(ParserTest, ExportExpressionStmt)
{
    program = parse(CreateTempFile("int calc() { 5 + 3; }"));
    ...
}

TEST_F(ParserTest, ImportPrematureEOF)
{
    int fd = CreateAstFile();
    EXPECT_DEATH(import_ast(fd), "Premature EOF");
}

TEST_F(ParserTest, ImportInvalidTag)
{
    int fd = CreateAstFile();
    size_t tag = -1;
    write(fd, &tag, sizeof(tag)); // Invalid tag
    EXPECT_DEATH(import_ast(fd), "Expected TAG_PROGRAM");
    close(fd);
}

TEST_F(ParserTest, ImportInputError)
{
    program = new_program();

    int fd = CreateAstFile();
    export_ast(fd, program);

    mock_error = true; // TODO
    EXPECT_DEATH(import_ast(fd), "Input error");
}
#endif
