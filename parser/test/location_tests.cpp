//
// Source locations of the AST nodes: a node starts at its first token, an operator
// node at its operator, a declarator at its name.
//
#include <unistd.h>

#include "fixture.h"

static const char *located_source = R"(int g = 1;
int f(int a, int b)
{
    int x = a + b;
    if (x)
        x = -f(x, 1);
    return x ? a : b;
}
)";

static void ExpectLoc(SrcLoc loc, int line, int col)
{
    EXPECT_EQ(loc.line, line);
    EXPECT_EQ(loc.col, col);
}

static void CheckLocations(Program *program)
{
    ExternalDecl *g = program->decls;
    ASSERT_NE(nullptr, g);
    ExpectLoc(g->loc, 1, 1);
    ExpectLoc(g->u.declaration->loc, 1, 1);
    ExpectLoc(g->u.declaration->u.var.declarators->loc, 1, 5);
    ExpectLoc(g->u.declaration->u.var.declarators->init->loc, 1, 9);

    ExternalDecl *f = g->next;
    ASSERT_NE(nullptr, f);
    ExpectLoc(f->loc, 2, 5);
    Param *a = f->u.function.type->u.function.params;
    ASSERT_NE(nullptr, a);
    ExpectLoc(a->loc, 2, 11);
    ASSERT_NE(nullptr, a->next);
    ExpectLoc(a->next->loc, 2, 18);

    Stmt *body = f->u.function.body;
    ExpectLoc(body->loc, 3, 1);

    // int x = a + b;
    DeclOrStmt *item = body->u.compound;
    ASSERT_NE(nullptr, item);
    Declaration *x = item->u.decl;
    ExpectLoc(x->loc, 4, 5);
    InitDeclarator *xd = x->u.var.declarators;
    ExpectLoc(xd->loc, 4, 9);
    Expr *sum = xd->init->u.expr;
    ExpectLoc(sum->loc, 4, 15);
    ExpectLoc(sum->u.binary_op.left->loc, 4, 13);
    ExpectLoc(sum->u.binary_op.right->loc, 4, 17);

    // if (x) x = -f(x, 1);
    item = item->next;
    ASSERT_NE(nullptr, item);
    Stmt *if_stmt = item->u.stmt;
    ExpectLoc(if_stmt->loc, 5, 5);
    Stmt *then_stmt = if_stmt->u.if_stmt.then_stmt;
    ExpectLoc(then_stmt->loc, 6, 9);
    Expr *assign = then_stmt->u.expr;
    ExpectLoc(assign->loc, 6, 11);
    Expr *neg = assign->u.assign.value;
    ExpectLoc(neg->loc, 6, 13);
    Expr *call = neg->u.unary_op.expr;
    ExpectLoc(call->loc, 6, 15);
    ExpectLoc(call->u.call.func->loc, 6, 14);
    ExpectLoc(call->u.call.args->next->loc, 6, 19);

    // return x ? a : b;
    item = item->next;
    ASSERT_NE(nullptr, item);
    Stmt *ret = item->u.stmt;
    ExpectLoc(ret->loc, 7, 5);
    ExpectLoc(ret->u.expr->loc, 7, 14);
}

TEST_F(ParserTest, NodeLocations)
{
    program = parse(CreateTempFile(located_source));
    ASSERT_NE(nullptr, program);
    CheckLocations(program);
}

// Line markers name the file and number the lines that follow.
TEST_F(ParserTest, LocationAfterLineMarker)
{
    program = parse(CreateTempFile("# 1 \"main.c\"\nint a;\n# 40 \"inc.h\" 1\n  int b;\n"));
    ASSERT_NE(nullptr, program);
    ExternalDecl *a = program->decls;
    ASSERT_NE(nullptr, a);
    EXPECT_STREQ(a->loc.file, "main.c");
    ExpectLoc(a->loc, 1, 1);
    ExternalDecl *b = a->next;
    ASSERT_NE(nullptr, b);
    EXPECT_STREQ(b->loc.file, "inc.h");
    ExpectLoc(b->loc, 40, 3);
}

// The binary AST keeps the locations, file names included.
TEST_F(ParserTest, LocationsSurviveExport)
{
    program = parse(CreateTempFile(("# 1 \"t.c\"\n" + std::string(located_source)).c_str()));
    ASSERT_NE(nullptr, program);

    int fd = CreateAstFile();
    export_ast(fd, program);
    Program *imported = import_ast(fd);
    close(fd);
    CheckLocations(imported);
    EXPECT_STREQ(imported->decls->loc.file, "t.c");
    EXPECT_STREQ(imported->decls->next->u.function.body->loc.file, "t.c");
    free_program(imported);
}

// A syntax error names the file, line and column of the offending token.
TEST_F(ParserTest, SyntaxErrorLocation)
{
    EXPECT_DEATH(parse(CreateTempFile("# 1 \"bad.c\"\nint main(void)\n{\n    return 1 +;\n}\n")),
                 "bad\\.c:3:15: error: expected an expression before ';'");
}
