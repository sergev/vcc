#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "parser_internal.h"
#include "scanner.h"
#include "xalloc.h"

/* Global lexer state */
int current_token;
static int peek_token;
const char *current_lexeme;
static char lexeme_buffer[1024]; // Buffer for current lexeme

// Name of the function being defined, for __func__; NULL outside a function body.
const char *current_function_name;

// Enable debug output
int parser_debug;

// Level of scope for nested compound operators, from semantic.
extern int scope_level;

//
// Helpers for fatal_message.
//
int parser_get_token()
{
    return current_token;
}

const char *parser_get_lexeme()
{
    return current_lexeme;
}

//
// In case of identifier, look in the parser's symbol table.
// When it's a previously defined typedef - return TOKEN_TYPEDEF_NAME.
// When a previously defined enumerator - return TOKEN_ENUMERATION_CONSTANT.
//
static int token_translation(int token)
{
    // Check identifier type
    if (token == TOKEN_IDENTIFIER) {
        token = nametab_find(get_yytext());
        if (!token) {
            token = TOKEN_IDENTIFIER;
        }
    }
    if (parser_debug) {
        printf("--- token %d '%s'\n", token, get_yytext());
    }
    return token;
}

/* Token handling */
void advance_token()
{
    if (peek_token > 0) {
        current_token = peek_token;
        peek_token    = 0;
    } else {
        current_token = token_translation(yylex());
    }
    current_lexeme = get_yytext();
}

// Is current token valid but different from the given one?
bool current_token_is_not(int token)
{
    return (current_token != TOKEN_EOF) && (current_token != token);
}

// Does this token have something valuable in yytext?
static bool has_yytext(int token)
{
    return token == TOKEN_IDENTIFIER || token == TOKEN_I_CONSTANT || token == TOKEN_F_CONSTANT ||
           token == TOKEN_ENUMERATION_CONSTANT || token == TOKEN_STRING_LITERAL ||
           token == TOKEN_TYPEDEF_NAME;
}

// Peek next token, without advancing the parser.
int next_token()
{
    if (peek_token == 0) {
        if (has_yytext(current_token)) {
            // Make a copy of current lexeme.
            strcpy(lexeme_buffer, current_lexeme);
            current_lexeme = lexeme_buffer;
        }
        peek_token = token_translation(yylex());
    }
    return peek_token;
}

void expect_token(int expected)
{
    if (current_token != expected) {
        fprintf(stderr, "Parse error: expected %s, got %s", token_name(expected),
                token_name(current_token));
        if (current_lexeme && current_lexeme[0]) {
            fprintf(stderr, " (lexeme: %s)", current_lexeme);
        }
        fputc('\n', stderr);
        exit(1);
    }
    advance_token();
}

// Is this token a type specifier?
bool is_type_specifier(int token)
{
    return token == TOKEN_VOID || token == TOKEN_CHAR || token == TOKEN_SHORT ||
           token == TOKEN_INT || token == TOKEN_LONG || token == TOKEN_FLOAT ||
           token == TOKEN_DOUBLE || token == TOKEN_SIGNED || token == TOKEN_UNSIGNED ||
           token == TOKEN_BOOL || token == TOKEN_COMPLEX || token == TOKEN_IMAGINARY ||
           token == TOKEN_STRUCT || token == TOKEN_UNION || token == TOKEN_ENUM ||
           token == TOKEN_TYPEDEF_NAME;
}

// Is this token a type qualifier?
// Except _Atomic.
bool is_type_qualifier(int token)
{
    return token == TOKEN_CONST || token == TOKEN_RESTRICT || token == TOKEN_VOLATILE;
}

// Is this token a storage class specifier?
bool is_storage_class_specifier(int token)
{
    return token == TOKEN_TYPEDEF || token == TOKEN_EXTERN || token == TOKEN_STATIC ||
           token == TOKEN_THREAD_LOCAL || token == TOKEN_AUTO || token == TOKEN_REGISTER;
}

//
// translation_unit
//     : external_declaration
//     | translation_unit external_declaration
//     ;
//
Program *parse_translation_unit()
{
    if (parser_debug) {
        printf("--- %s()\n", __func__);
    }
    Program *program = new_program();
    while (current_token != TOKEN_EOF) {
        ExternalDecl *decl = parse_external_declaration();
        append_list(&program->decls, decl);
    }
    return program;
}

//
// external_declaration
//     : function_definition
//     | declaration
//     ;
//
ExternalDecl *parse_external_declaration()
{
    if (parser_debug) {
        printf("--- %s()\n", __func__);
    }
    if (current_token == TOKEN_STATIC_ASSERT) {
        // Static assert.
        ExternalDecl *ed  = new_external_decl(EXTERNAL_DECL_DECLARATION);
        ed->u.declaration = parse_static_assert_declaration();
        return ed;
    }

    // Parse declaration_specifiers (common to both function_definition and declaration).
    Type *base_type = NULL;
    DeclSpec *spec  = parse_declaration_specifiers(&base_type);

    // Check if it's a declaration (ends with ';').
    if (current_token == TOKEN_SEMICOLON) {
        // Empty declaration.
        advance_token();
        Declaration *decl        = new_declaration(DECL_EMPTY);
        decl->u.empty.specifiers = spec;
        decl->u.empty.type       = base_type;

        ExternalDecl *ed  = new_external_decl(EXTERNAL_DECL_DECLARATION);
        ed->u.declaration = decl;
        return ed;
    }

    // Parse declarator (required for both function_definition and declaration with
    // init_declarator_list).
    Declarator *decl = parse_declarator();

    // Check if it's a declaration with init_declarator_list.
    if (current_token == TOKEN_SEMICOLON || current_token == TOKEN_COMMA ||
        current_token == TOKEN_ASSIGN) {
        // Declaration of variables.
        ExternalDecl *ed  = new_external_decl(EXTERNAL_DECL_DECLARATION);
        ed->u.declaration = new_declaration(DECL_VAR);

        ed->u.declaration->u.var.specifiers  = spec;
        ed->u.declaration->u.var.declarators = parse_init_declarator_list(decl, base_type);
        if (is_typedef(spec)) {
            define_typedef(ed->u.declaration->u.var.declarators);
        }
        expect_token(TOKEN_SEMICOLON);
        free_type(base_type);
        return ed;
    }

    // Must be a function_definition.
    Declaration *decl_list = NULL;
    if (current_token_is_not(TOKEN_LBRACE)) {
        decl_list = parse_declaration_list();
    }

    ExternalDecl *ed          = new_external_decl(EXTERNAL_DECL_FUNCTION);
    ed->u.function.specifiers = spec;
    ed->u.function.name       = xstrdup(decl->name);
    ed->u.function.type =
        type_apply_suffixes(type_apply_pointers(base_type, decl->pointers), decl->suffixes);
    ed->u.function.param_decls = decl_list;
    current_function_name      = ed->u.function.name;
    ed->u.function.body        = parse_compound_statement();
    current_function_name      = NULL;
    free_declarator(decl);
    return ed;
}

//
// declaration_list
//     : declaration
//     | declaration_list declaration
//     ;
//
Declaration *parse_declaration_list()
{
    if (parser_debug) {
        printf("--- %s()\n", __func__);
    }
    Declaration *decl = parse_declaration();
    if (current_token_is_not(TOKEN_LBRACE)) {
        decl->next = parse_declaration_list();
    }
    return decl;
}

/* Main parsing function */
Program *parse(FILE *input)
{
    if (parser_debug) {
        printf("--- %s()\n", __func__);
    }
    init_scanner(input);
    reset_anon_tag_counter(); // synthetic anonymous tags are numbered per translation unit
    current_function_name = NULL;
    advance_token();
    Program *program = parse_translation_unit();
    if (current_token != TOKEN_EOF) {
        fatal_error("Expected end of file");
    }
    return program;
}
