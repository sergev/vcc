//
// Chapter 18 -- Structures and unions: invalid lexer/parser input.  Imported
// from "Writing a C Compiler" (tests/chapter_18/invalid_lex and invalid_parse,
// including extra_credit).  Each program is malformed at the token or grammar
// level; tests assert on a substring of the scanner/parser diagnostic.
//
// Three small parser fixes (parser/decl.c) light up programs we previously
// accepted: a basic type specifier combined with a struct/union/enum/typedef
// specifier, a struct/union member with no declarator, and an empty initializer
// list `{}` (invalid in C11).  The two "member is a function" programs are
// caught later, during type checking, so they live in chapter18_tests.cpp under
// semantic/.
//
#include "fixture.h"


// --- invalid_lex ---

TEST_F(ParserTest, Chapter18_InvalidLexDotBadToken_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s x;
    // we should reject .1l as an invalid preprocessing number;
    // we shouldn't lex it as a dot followed by a valid constant
    return x.1l;
}
)SRC")),
                 "expected ';' before '\\.1l'");
}

TEST_F(ParserTest, Chapter18_InvalidLexDotBadToken2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a;
};

int main(void) {
    struct s x;
    // Recognize .0foo as an invalid token instead of a struct member operator
    return x.0foo;
}
)SRC")),
                 "invalid suffix on numeric constant '\\.0fo'");
}


// --- invalid_parse ---

TEST_F(ParserTest, Chapter18_InvalidParseArrowMissingMember_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int y;
};

int main(void) {
    struct s *ptr = 0;
    return ptr->;  // arrow must be followed by a member name
}
)SRC")),
                 "expected identifier before ';'");
}

TEST_F(ParserTest, Chapter18_InvalidParseDotInvalidMember_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int y;
};

struct s x;
// dot operator must be immediately followed by member name
// (can't parenthesize it)
int main(void) {
    return x.(y);
}
)SRC")),
                 "expected identifier before '\\('");
}

TEST_F(ParserTest, Chapter18_InvalidParseDotNoLeftExpr_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
int main(void) {
    return .a;  // a dot operator can only appear after an expression
}
)SRC")),
                 "expected an expression before '\\.'");
}

TEST_F(ParserTest, Chapter18_InvalidParseDotOperatorInDeclarator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct pair {
    int a;
    int b;
};

int main(void) {
    // you can't use the dot operator to specify one member in a struct
    // declarator; you need a compound initializer to initialize structure
    // members
    struct pair x.a = 10;
}
)SRC")),
                 "expected ';' before '\\.'");
}

// An empty initializer list `{}` is accepted (valid as of C23).
TEST_F(ParserTest, Chapter18_EmptyInitializerList)
{
    DeclOrStmt *body = GetFunctionBody(R"SRC(
struct s {int a;};

int main(void) {
    struct s foo = {};
    return 0;
}
)SRC");
    ASSERT_EQ(DECL_OR_STMT_DECL, body->kind);
    Initializer *init = body->u.decl->u.var.declarators->init;
    ASSERT_NE(nullptr, init);
    EXPECT_EQ(INITIALIZER_COMPOUND, init->kind);
    EXPECT_EQ(nullptr, init->u.items);
}


// --- invalid_parse/extra_credit ---

TEST_F(ParserTest, Chapter18_ExtraCreditCaseStructDecl_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Because a structure declaration isn't a statement, it can't appear right
// after a case statement. NOTE: this is valid as of C23
int main(void) {
    switch (0) {
        case 0:
            struct s {
                int a;
            };
            return 0;
    }
    return 0;
}
)SRC")),
                 "expected an expression before 'struct'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditDefaultKwMemberName_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// because 'default' is a keyword, we can't use it as a member name
struct s {
    int default;
};

int main(void) {
    return 0;
}
)SRC")),
                 "expected an identifier or '\\(' before 'default'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditGotoKwStructTag_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Because 'goto' is a keyword, we can't use it as a struct tag
struct goto { int a; };
int main(void) {
    return 0;
}
)SRC")),
                 "expected an identifier or '\\(' before 'goto'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditLabelInsideStructDecl_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
/* Labels can't appear inside structure declarations */

struct s {
    int i;
    // NOTE: GCC and clang will treat foo as a bit-field (a feature we aren't
    // implementing. If you implement bit-fields, this is still a parse error
    foo : int j;
};

int main(void) {
    return 0;
}
)SRC")),
                 "expected a type before 'foo'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditLabeledStructDecl_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Because a structure declaration isn't a statement, it can't appear right
// after a label. NOTE: this is valid as of C23
int main(void) {
foo:
    struct s {
        int a;
    };
    return 0;
}
)SRC")),
                 "expected an expression before 'struct'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditStructUnion_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Can't use struct and union keywords in same declaration
union struct s {
    int a;
};

int main(void) {
    return 0;
}
)SRC")),
                 "cannot combine 'struct' with another type");
}

TEST_F(ParserTest, Chapter18_ExtraCreditTwoUnionKws_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
union u {
    int a;
};

union union u x;  // can't use union keyword twice in a type specifier

int main(void) {
    return 0;
}
)SRC")),
                 "cannot combine 'union' with another type");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionBadTypeSpec_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Can't combine union type specifier with other type specifier
union x long a;
)SRC")),
                 "cannot combine a type specifier with a struct, union, enum or typedef name");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionDeclBadTypeSpecifier_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Can't combine union specifier with other type specifier

union a { int a; };

int main(void) {
    union a int x;
    return 0;
}
)SRC")),
                 "cannot combine a type specifier with a struct, union, enum or typedef name");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionDeclEmptyMemberList_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// union member list cannot be empty
// (note that GCC/Clang allow this as an extenision)
union s {};

int main(void) {
    return 0;
}
)SRC")),
                 "expected a type before '\\}'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionDeclExtraSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Stray semicolon in union declaration
union u {
    int a;
    ;
};

int main(void) {
    return 0;
}
)SRC")),
                 "expected a type before ';'");
}

// An empty initializer list `{}` for a union is accepted (valid as of C23).
TEST_F(ParserTest, Chapter18_ExtraCreditUnionEmptyInitializer)
{
    DeclOrStmt *body = GetFunctionBody(R"SRC(
union u { int a; };

int main(void) {
    union u x = {};
    return 0;
}
)SRC");
    ASSERT_EQ(DECL_OR_STMT_DECL, body->kind);
    Initializer *init = body->u.decl->u.var.declarators->init;
    ASSERT_NE(nullptr, init);
    EXPECT_EQ(INITIALIZER_COMPOUND, init->kind);
    EXPECT_EQ(nullptr, init->u.items);
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionMemberInitializer_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
union a {
    // union members cannot have initializers
    int member = 1;
};
)SRC")),
                 "expected ',' before '='");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionMemberNameKw_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// Can't use a keyword as a union member name

union u {
    int struct;
};
)SRC")),
                 "cannot combine 'struct' with another type");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionMemberNoDeclarator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
union u {
    int;  // every union member needs a declarator
    // we treat this as a parse error but catching it in the type checker would
    // also be reasonable
    // NOTE: a union member with anonymous struct or
    // union type doesn't need a declarator (but we don't support anonymous
    // structs/unions)
};
)SRC")),
                 "member declaration does not declare anything");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionMemberNoType_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
union u {
    a;  // each union member declaration must specify a type
};
)SRC")),
                 "expected a type before 'a'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionMemberStorageClass_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
union y {
    // union member cannot have storage class
    static int a;
};
)SRC")),
                 "expected a type before 'static'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionStructTag_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// can't use 'struct' as the keyword for a union declaration
union struct {
    int a;
};

int main(void) {
    return 0;
}
)SRC")),
                 "cannot combine 'struct' with another type");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionTwoTags_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
/* Union tag must be a single identifier */

union x y {
    int a;
};
)SRC")),
                 "expected a type before ';'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionVarBadTag_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
int main(void) {
    union 4 foo;  // a union tag must be an identifier (not a constant)
    return 0;
}
)SRC")),
                 "expected an identifier or '\\(' before '4'");
}

TEST_F(ParserTest, Chapter18_ExtraCreditUnionVarTagParen_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int y;
};

int main(void) {
    // can't parenthesize union tag
    union(s) var;

    return 0;
}
)SRC")),
                 "expected ';' before 'var'");
}


// --- invalid_parse ---

TEST_F(ParserTest, Chapter18_InvalidParseMisplacedStorageClass_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a;
};

// storage class specifier can't come between struct keyword and tag
struct static s foo;
)SRC")),
                 "expected a type before 'foo'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclDoubleSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a;
    ;  // extra semicolon that doesn't follow declaration is a syntax error
};
)SRC")),
                 "expected a type before ';'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclEmptyMemberList_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// struct member list cannot be empty
// (note that GCC/Clang allow this as an extenision)
struct s {};

int main(void) {
    return 0;
}
)SRC")),
                 "expected a type before '\\}'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclExtraSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    ;  // extra semicolon that doesn't follow declaration is a syntax error
    int a;
};
)SRC")),
                 "expected a type before ';'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclKwWrongOrder_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// struct keyword must come before tag
s struct x { int a; };

int main(void) {
    return 0;
}
)SRC")),
                 "expected a type before 's'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclMissingEndSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a;
} // a structure declaration must end with a semicolon

int main(void) {
    return 0;
}
)SRC")),
                 "cannot combine a type specifier with a struct, union, enum or typedef name");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclTagKw_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// cannot use keyword (like 'for') as struct tag
struct for {
    int a;
};
)SRC")),
                 "expected an identifier or '\\(' before 'for'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructDeclTwoKws_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct struct s;  // struct keyword can only appear once in a type declaration

int main(void) {
    return 1;
}
)SRC")),
                 "cannot combine 'struct' with another type");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructMemberInitializer_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct a {
    // structure members cannot have initializers
    int member = 1;
};
)SRC")),
                 "expected ',' before '='");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructMemberNameKw_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
// cannot use a keyword (like 'return') as a field name

struct s {
    int return;
}

int main(void) {
    return 0;
}
)SRC")),
                 "expected an identifier or '\\(' before 'return'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructMemberNoDeclarator_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int;  // every structure member needs a declarator
    // we treat this as a parse error but catching it in the type checker would
    // also be reasonable
    // NOTE: a structure member with anonymous struct or
    // union type doesn't need a declarator (but we don't support anonymous
    // structs/unions)
};
)SRC")),
                 "member declaration does not declare anything");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructMemberNoSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a  // structure member declaration must end with a semicolon
};

int main(void) {
    return 0;
}
)SRC")),
                 "expected ',' before '\\}'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructMemberNoType_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    a;  // each structure member declaration must specify a type
};
)SRC")),
                 "expected a type before 'a'");
}

TEST_F(ParserTest, Chapter18_InvalidParseStructMemberStorageClass_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct y {
    // structure member cannot have storage class
    static int a;
};
)SRC")),
                 "expected a type before 'static'");
}

TEST_F(ParserTest, Chapter18_InvalidParseVarDeclBadTag1_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
int main(void) {
    struct 4 foo;  // a struct tag must be an identifier (not a constant)
    return 0;
}
)SRC")),
                 "expected an identifier or '\\(' before '4'");
}

TEST_F(ParserTest, Chapter18_InvalidParseVarDeclBadTag2_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int y;
};

int main(void) {
    // can't parenthesize struct tag
    struct(s) var;

    return 0;
}
)SRC")),
                 "expected ';' before 'var'");
}

TEST_F(ParserTest, Chapter18_InvalidParseVarDeclBadTypeSpecifier_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s;

// cannot combine struct keyword with other type specifier
struct s long a;

int main(void) {
    return 0;
}
)SRC")),
                 "cannot combine a type specifier with a struct, union, enum or typedef name");
}

TEST_F(ParserTest, Chapter18_InvalidParseVarDeclMissingStructKw_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
int main(void) {
    struct x;
    x y;  // you can't specify a structure type with just the tag; you need the
          // 'struct' specifier
    return 0;
}
)SRC")),
                 "expected ';' before 'y'");
}

TEST_F(ParserTest, Chapter18_InvalidParseVarDeclTwoStructKws_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
struct s {
    int a;
};

struct struct s x;  // can't use struct keyword twice in a type specifier

int main(void) {
    return 0;
}
)SRC")),
                 "cannot combine 'struct' with another type");
}

TEST_F(ParserTest, Chapter18_InvalidParseVarDeclTwoTags_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile(R"SRC(
/* Structure tag must be a single identifier */

struct x y {
    int a;
};
)SRC")),
                 "expected a type before ';'");
}
