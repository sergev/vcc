# C Grammar Reference

This document explains the three grammar specification files in `grammar/` — `c11.l`, `c11.y`,
and `c11.asdl` — what role each plays in describing the C language, and how they relate to the
compiler's hand-written implementation.

## Overview

Compiling C requires three distinct layers of understanding, each building on the last.

**Lexical structure** is the lowest layer: given a stream of characters, which substrings are
meaningful units? The word `return` is a keyword; `123` is an integer constant; `+=` is an
operator. A *scanner* (also called a *lexer* or *tokenizer*) reads raw characters and emits a
stream of classified tokens.

**Syntactic structure** is the middle layer: given a stream of tokens, which sequences form valid
C phrases? `int x = 5;` is a valid declaration; `int = x 5` is not. A *parser* reads tokens and
builds a tree that reflects the grammatical structure of the program.

**Abstract structure** is the top layer: what information do we actually need to preserve for later
compiler phases? The parse tree is cluttered with punctuation tokens, redundant nesting, and
concrete details that carry no semantic weight. An *abstract syntax tree* (AST) distills the
program down to the parts that matter — types, declarations, expressions, and statements — in a
representation that is convenient to analyze and transform.

Three files in `grammar/` give a precise formal specification of each layer:

| File | Layer | Tool format |
|------|-------|-------------|
| `c11.l` | Lexical scanner | Lex/Flex specification |
| `c11.y` | Context-free grammar | Yacc/Bison grammar |
| `c11.asdl` | Abstract syntax | Zephyr ASDL notation |

None of these files are used to generate code. The compiler's scanner (`scanner/`), parser
(`parser/`), and AST (`ast/`) are all written by hand. The grammar files serve as formal
reference specifications: precise enough to be mechanically checked, detailed enough to settle
ambiguity questions when implementing or extending the compiler.

## The Lexical Scanner (`c11.l`)

### What a scanner does

A scanner's job is to turn a flat sequence of characters into a sequence of *tokens*. Each token
has a *kind* (keyword, identifier, integer constant, string literal, operator, punctuation) and
a *lexeme* — the original text it was matched from.

The C standard defines a *translation phase model*. By the time the scanner sees the source file,
phases 1 through 5 have already run: trigraphs are translated, line splices are joined, the
preprocessor has expanded macros and handled `#include` directives. The scanner operates on the
resulting preprocessed text. `c11.l` documents this assumption explicitly in its header comment.

### The Flex specification format

A `.l` file has two sections separated by `%%`. Above the separator is the *definition section*:
character class macros and any C code that should appear in the generated source. Below it are
the *rules*: a list of regular expression → action pairs. When the scanner reads input, it finds
the longest match among all rules and runs the corresponding action — usually `return(TOKEN)`.

The file ends with an optional third section of raw C helper functions.

### Character class macros

`c11.l` opens with a compact vocabulary of named patterns that the rules reuse:

```
O   [0-7]              # octal digit
D   [0-9]              # decimal digit
NZ  [1-9]              # non-zero digit (used to rule out octal in decimal literals)
L   [a-zA-Z_]          # identifier start character
A   [a-zA-Z_0-9]       # identifier continuation character
H   [a-fA-F0-9]        # hexadecimal digit
HP  (0[xX])            # hexadecimal prefix
E   ([Ee][+-]?{D}+)    # decimal exponent (for floating-point)
P   ([Pp][+-]?{D}+)    # binary exponent (for hexadecimal floating-point)
FS  (f|F|l|L)          # floating-point suffix
IS  (((u|U)(l|L|ll|LL)?)|((l|L|ll|LL)(u|U)?))  # integer suffix (u, ul, ull, …)
CP  (u|U|L)            # character prefix
SP  (u8|u|U|L)         # string prefix
ES  (\\(['"\?\\abfnrtv]|[0-7]{1,3}|x[a-fA-F0-9]+))  # escape sequence
WS  [ \t\v\n\f]        # whitespace
```

These macros map almost directly to the grammar productions in the C standard. Having them
named makes the rules below readable and keeps the specification close to the standard's own
language.

### Keywords

C11 has 37 reserved keywords. `c11.l` lists each one individually before the identifier rule:

```lex
"auto"          { return(AUTO); }
"break"         { return(BREAK); }
...
"_Alignas"      { return ALIGNAS; }
"_Alignof"      { return ALIGNOF; }
"_Atomic"       { return ATOMIC; }
"_Bool"         { return BOOL; }
"_Generic"      { return GENERIC; }
"_Noreturn"     { return NORETURN; }
"_Static_assert" { return STATIC_ASSERT; }
"_Thread_local" { return THREAD_LOCAL; }
"__func__"      { return FUNC_NAME; }
```

Because Lex/Flex always prefers a longer match, and breaks ties by preferring rules listed
earlier, keyword rules before the identifier rule ensure that `int` returns `INT`, not
`IDENTIFIER`. Without this ordering, every keyword would be treated as an identifier.

### Token categories

After the keyword rules, a single rule handles all identifiers:

```lex
{L}{A}*   { return check_type(); }
```

The literal pattern `{L}{A}*` matches a letter or underscore followed by any number of
alphanumeric characters or underscores — the C identifier syntax. Rather than returning
`IDENTIFIER` directly, the rule calls `check_type()`, explained below.

**Integer and character constants** are handled by four rules:

```lex
{HP}{H}+{IS}?                       { return I_CONSTANT; }   // 0x1A2BuLL
{NZ}{D}*{IS}?                       { return I_CONSTANT; }   // 42, 100u
"0"{O}*{IS}?                        { return I_CONSTANT; }   // 0, 0755
{CP}?"'"([^'\\\n]|{ES})+"'"         { return I_CONSTANT; }   // 'a', L'\n', u'A'
```

All four return the same `I_CONSTANT` token kind — the parser does not need to distinguish
decimal from octal from hexadecimal at the grammar level; that distinction matters only during
constant evaluation in later phases.

**Floating-point constants** cover the decimal and hexadecimal forms specified by C11:

```lex
{D}+{E}{FS}?                        { return F_CONSTANT; }   // 1e5, 3.14f
{D}*"."{D}+{E}?{FS}?                { return F_CONSTANT; }   // .5, 3.14
{D}+"."{E}?{FS}?                    { return F_CONSTANT; }   // 3., 3.e5
{HP}{H}+{P}{FS}?                    { return F_CONSTANT; }   // 0x1p4
{HP}{H}*"."{H}+{P}{FS}?             { return F_CONSTANT; }   // 0x1.8p+4
{HP}{H}+"."{P}{FS}?                 { return F_CONSTANT; }   // 0x1.p0
```

**String literals** are matched by a rule that handles translation phase 6 in a single pass.
Phase 6 requires adjacent string literals to be concatenated: `"hello" " " "world"` becomes one
string. The rule achieves this by allowing the string literal pattern to repeat, with optional
whitespace between repetitions:

```lex
({SP}?\"([^"\\\n]|{ES})*\"{WS}*)+  { return STRING_LITERAL; }
```

The outer `(...)+` makes the scanner consume all adjacent literals before returning a single
`STRING_LITERAL` token. (The actual concatenation of the text is a later step.)

**Multi-character operators** appear before single-character punctuation so the longer match
wins:

```lex
">>="  { return RIGHT_ASSIGN; }
"<<="  { return LEFT_ASSIGN; }
"..."  { return ELLIPSIS; }
"++"   { return INC_OP; }
"->"   { return PTR_OP; }
"&&"   { return AND_OP; }
...
```

Single-character operators just return their ASCII code as the token kind:

```lex
";"    { return ';'; }
"+"    { return '+'; }
"*"    { return '*'; }
```

Whitespace is silently consumed. Any character that matches nothing else is silently discarded
(`"." { /* discard bad characters */ }`), which keeps the scanner from aborting on isolated
bad bytes.

### The typedef disambiguation problem

C has a well-known lexical ambiguity: the same identifier can be a type name or a variable name
depending on what has been declared before the current point in the file. Without additional
context, `foo * bar` is ambiguous — it could be a multiplication expression or a declaration of
`bar` as a pointer to `foo`. A Yacc grammar cannot resolve this from syntax alone; the lexer
must distinguish `TYPEDEF_NAME` from ordinary `IDENTIFIER` tokens.

The `check_type()` function embodies this solution:

```c
static int check_type(void)
{
    switch (sym_type(yytext)) {
    case TYPEDEF_NAME:           return TYPEDEF_NAME;
    case ENUMERATION_CONSTANT:   return ENUMERATION_CONSTANT;
    default:                     return IDENTIFIER;
    }
}
```

`sym_type()` is called with the current lexeme. It consults a symbol table that the parser
maintains as it processes declarations. When the parser sees `typedef int MyInt;`, it registers
`MyInt` in the symbol table as a type name. The next time `MyInt` appears as an identifier, the
lexer returns `TYPEDEF_NAME` instead of `IDENTIFIER`, and the grammar can parse the surrounding
context correctly.

Enumeration constants face the same issue: an enumerator like `RED` looks syntactically identical
to a variable name. The scanner returns `ENUMERATION_CONSTANT` once the parser has registered it.

This feedback loop between the parser's symbol table and the lexer's token classification is
sometimes called the "lexer hack." It is not really a hack — it is the correct, standard
mechanism for resolving C's ambiguity — but it does mean a purely context-free grammar cannot
describe C without this auxiliary state.

### Comment handling

C has two comment forms. Line comments (`// ...`) are handled inline:

```lex
"//".*   { /* consume //-comment */ }
```

Block comments (`/* ... */`) are more involved because they can span multiple lines and cannot
be described by a simple regular expression that a finite automaton handles cleanly. The lexer
calls a helper function `comment()` that reads characters one at a time until it finds `*/`,
being careful to detect unterminated comments and report an error.

### Relationship to the hand-written scanner

The hand-written scanner in `scanner/scanner.c` produces exactly the same token set as `c11.l`.
The token names differ only in having a `TOKEN_` prefix — `TOKEN_IDENTIFIER`, `TOKEN_I_CONSTANT`,
`TOKEN_TYPEDEF_NAME`, and so on — but the categories, the character patterns, and the
typedef-disambiguation mechanism are identical. `c11.l` is the specification; `scanner/` is the
implementation.

## The Grammar Parser (`c11.y`)

### What a parser does

A parser reads a token stream and determines whether it constitutes a valid program according to
the language's grammar. For every grammatical construct it recognizes, it typically builds a
node in a *parse tree* that records the structure. The parse tree has one node per grammar rule
that matched, including all the punctuation and redundant grouping; later the compiler strips this
down to an AST.

### The Yacc/Bison specification format

A `.y` file has two sections separated by `%%`. The first section contains *token declarations*
(`%token`) that name every terminal symbol the grammar uses, plus any C code in `%{ ... %}` to
be inserted verbatim at the top of the generated source. The second section contains the
*grammar rules* themselves: non-terminal definitions of the form `name : alternative1 | alternative2 ;`.

Yacc/Bison generates an LALR(1) parser — a table-driven parser that can look one token ahead to
decide how to proceed. The generated C source implements a push-down automaton driven by those
tables.

### Token declarations

`c11.y` declares tokens in four logical groups:

```yacc
%token IDENTIFIER I_CONSTANT F_CONSTANT STRING_LITERAL FUNC_NAME SIZEOF
%token PTR_OP INC_OP DEC_OP LEFT_OP RIGHT_OP LE_OP GE_OP EQ_OP NE_OP
%token AND_OP OR_OP MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN ADD_ASSIGN
%token SUB_ASSIGN LEFT_ASSIGN RIGHT_ASSIGN AND_ASSIGN XOR_ASSIGN OR_ASSIGN
%token TYPEDEF_NAME ENUMERATION_CONSTANT

%token TYPEDEF EXTERN STATIC AUTO REGISTER INLINE
%token CONST RESTRICT VOLATILE
%token BOOL CHAR SHORT INT LONG SIGNED UNSIGNED FLOAT DOUBLE VOID
%token COMPLEX IMAGINARY STRUCT UNION ENUM ELLIPSIS

%token CASE DEFAULT IF ELSE SWITCH WHILE DO FOR GOTO CONTINUE BREAK RETURN

%token ALIGNAS ALIGNOF ATOMIC GENERIC NORETURN STATIC_ASSERT THREAD_LOCAL
```

Single-character tokens like `+`, `*`, `{`, and `;` are not declared — their ASCII values serve
as token codes directly.

### Operator precedence through grammar hierarchy

C's expression grammar is not flat. There are fifteen levels of precedence among operators, and
associativity differs: `+` is left-associative, assignment is right-associative, the ternary
operator chains right-to-right. A Yacc grammar encodes these rules implicitly through rule
nesting.

The hierarchy in `c11.y` runs from tightest to loosest binding:

| Grammar rule | C operators |
|---|---|
| `primary_expression` | literals, identifiers, `(expr)`, `_Generic` |
| `postfix_expression` | `a[i]`, `f(args)`, `a.b`, `a->b`, `a++`, `a--`, `(T){init}` |
| `unary_expression` | `++a`, `--a`, `&a`, `*a`, `+a`, `-a`, `~a`, `!a`, `sizeof`, `_Alignof` |
| `cast_expression` | `(type) expr` |
| `multiplicative_expression` | `*`, `/`, `%` |
| `additive_expression` | `+`, `-` |
| `shift_expression` | `<<`, `>>` |
| `relational_expression` | `<`, `>`, `<=`, `>=` |
| `equality_expression` | `==`, `!=` |
| `and_expression` | `&` (bitwise AND) |
| `exclusive_or_expression` | `^` |
| `inclusive_or_expression` | `\|` |
| `logical_and_expression` | `&&` |
| `logical_or_expression` | `\|\|` |
| `conditional_expression` | `? :` |
| `assignment_expression` | `=`, `+=`, `-=`, … |
| `expression` | `,` (comma operator) |

Each level's rule admits the level above it as a trivial alternative, creating the chain. For
example:

```yacc
additive_expression
    : multiplicative_expression
    | additive_expression '+' multiplicative_expression
    | additive_expression '-' multiplicative_expression
    ;
```

The only way to reach a `multiplicative_expression` from an `additive_expression` is via the
first alternative, which makes `*` bind tighter than `+` by construction — no explicit priority
declaration is needed.

### Declaration complexity

C declarations are notoriously hard to parse. The same tokens `*`, `[]`, and `()` appear in
both type specifiers and declarators, with different meaning in each position. The type
`int (*f)(char)` declares `f` as a pointer to a function taking `char` and returning `int` —
but reading that left-to-right, the precedence of `*` versus `()` is non-obvious.

The grammar handles this through two parallel hierarchies: *declarators* (for names being
declared) and *abstract declarators* (for type names used in casts and `sizeof`, where no name
appears). Both hierarchies allow arbitrarily complex nesting of pointer, array, and function
suffixes:

```yacc
declarator
    : pointer direct_declarator
    | direct_declarator
    ;

direct_declarator
    : IDENTIFIER
    | '(' declarator ')'
    | direct_declarator '[' assignment_expression ']'     // array
    | direct_declarator '(' parameter_type_list ')'       // function
    | direct_declarator '(' ')'
    ...
    ;

pointer
    : '*' type_qualifier_list pointer
    | '*' type_qualifier_list
    | '*' pointer
    | '*'
    ;
```

The recursive structure of `pointer` allows `***const` (pointer to pointer to const pointer)
without explicit limits; the left-recursion of `direct_declarator` builds up suffixes like
`[10][20]` or `(int, char)`.

The `declaration_specifiers` rule also requires careful grammar design. In C, the order of
specifiers within a declaration is flexible: `unsigned long int` and `int unsigned long` mean
the same thing. The grammar allows them to appear in any order by having `declaration_specifiers`
be right-recursive over all specifier types.

For K&R-style function definitions — an old form still legal in C11 — the grammar allows an
`identifier_list` in place of a full `parameter_type_list`, followed by a `declaration_list`
before the function body:

```yacc
function_definition
    : declaration_specifiers declarator declaration_list compound_statement
    | declaration_specifiers declarator compound_statement
    ;
```

### Shift/reduce conflicts

Two shift/reduce conflicts arise naturally from the C grammar. Bison documents both in the header
of `c11.y` and resolves them by its default shift-before-reduce rule, which is correct in each
case.

**The dangling else.** When an `if` statement has no `else`, and the immediately following token
is `else`, should the parser associate that `else` with the outer `if` or the inner one? By
convention, C associates `else` with the nearest `if`. Bison's default shift action produces
this behavior automatically: on seeing `else`, it always shifts (attaches to the nearest `if`)
rather than reducing (closing the inner `if` and giving `else` to the outer one).

**The `_Atomic` ambiguity.** `_Atomic` can appear as a type qualifier (in a list of qualifiers)
or as a function-like specifier `_Atomic(type_name)`. After seeing `_Atomic` and a `(`, the
parser cannot tell from one token of lookahead whether this is the start of `_Atomic(T)` or
the beginning of an expression. Again, Bison's default shift resolves this correctly by
treating `_Atomic (T)` as the qualified form when a valid type name follows.

### Relationship to the hand-written parser

The parser in `parser/parser.c` is a hand-written recursive-descent parser. It does not call
Yacc-generated tables; instead, each grammar non-terminal has a corresponding C function:
`parse_expression()`, `parse_declarator()`, `parse_statement()`, and so on. Those functions
follow the same rules as `c11.y` but execute them top-down rather than bottom-up.

Recursive descent is chosen over Yacc generation for three reasons. First, error recovery is
easier: a hand-written parser can skip tokens and resynchronize at logical boundaries, providing
useful error messages, while LALR error recovery is notoriously difficult to tune. Second, there
are no build-time dependencies on Bison or any other external tool. Third, the code is easier
to read, debug, and extend because the structure mirrors the grammar directly.

## The Abstract Syntax Definition (`c11.asdl`)

### What ASDL is

ASDL — Abstract Syntax Description Language — is a notation developed at CMU in 1997 for the
Zephyr compiler infrastructure. Its core idea is to describe the *shape* of an abstract syntax
tree independently of any particular programming language or implementation language.

A concrete grammar like `c11.y` describes how characters and tokens can legally be combined.
An ASDL definition like `c11.asdl` describes what that combination *means* — the information
content, stripped of punctuation, redundant grouping, and syntactic details that carry no
semantic weight.

ASDL types look like algebraic data types in functional languages. A *sum type* lists the
variants that a node can take:

```
Type = Void
     | Bool
     | Char(Signedness signed)
     | Int(Signedness signed)
     | Pointer(Type target, TypeQualifier* qualifiers)
     | ...
```

A *product type* (a named sequence of fields) describes a single variant with named components:

```
Param = Param(Ident? name, Type type)
```

`Ident?` means zero or one identifier (optional); `Type*` means zero or more types (a list).

### Types

The `Type` sum type covers the entire C type system in nineteen variants:

- **Scalar types**: `Void`, `Bool`, `Char(Signedness)`, `Short(Signedness)`, `Int(Signedness)`,
  `Long(Signedness)`, `Float`, `Double`
- **Complex and imaginary**: `Complex(Type base)`, `Imaginary(Type base)` (a non-mandated
  C11 extension for complex number arithmetic)
- **Derived types**: `Pointer(Type target, TypeQualifier* qualifiers)`,
  `Array(Type element, Expr? size, TypeQualifier* qualifiers)`,
  `Function(Type returnType, ParamList params, bool variadic)`
- **Composite types**: `Struct(Ident? name, Field* fields)`,
  `Union(Ident? name, Field* fields)`, `Enum(Ident? name, Enumerator* enumerators)`
- **Indirect types**: `TypedefName(Ident name)` (resolved later by the typecheck pass),
  `Atomic(Type base)` for `_Atomic`

The `attributes(TypeQualifier* qualifiers)` annotation means that every `Type` variant carries
an associated list of qualifiers — `const`, `volatile`, `restrict`, or `_Atomic`. This avoids
repeating the qualifier field in every variant.

The `ParamList` sum type handles the three parameter list forms C allows:

```
ParamList = ParamList(Param* params)    # normal: (int a, char b)
          | Empty                       # no parameters declared: ()
          | IdentList(Ident* idents)    # K&R style: (a, b)
```

### Declarations

The `Declaration` sum type captures what a declaration can be:

```
Declaration = VarDecl(DeclSpec specifiers, InitDeclarator* declarators)
            | StaticAssert(Expr condition, string message)
            | EmptyDecl     # declaration_specifiers ;  with no names
```

`DeclSpec` bundles the parts of a declaration that precede the declarator list: storage class,
type qualifiers, type specifiers, function specifiers (`inline`, `_Noreturn`), and alignment
specifier (`_Alignas`):

```
DeclSpec = DeclSpec(StorageClass? storage, TypeQualifier* qualifiers,
                    TypeSpec* typeSpecs, FunctionSpec* funcSpecs,
                    AlignmentSpec? alignSpec)
```

`Declarator` simplifies C's complex declarator syntax into a uniform structure: a name (optional
for abstract declarators), a list of pointer qualifiers, and a list of suffixes:

```
Declarator = Declarator(Ident name, Pointer* pointers, DeclaratorSuffix* suffixes)
           | AbstractDeclarator(Pointer* pointers, DeclaratorSuffix* suffixes)
```

Compare this to the grammar's `direct_declarator` rule, which has fourteen alternatives to handle
the recursive concrete syntax. The ASDL flattens that recursion into a clean list representation.

### Expressions

The `Expr` sum type uses one variant per expression kind:

```
Expr = Literal(Literal value)
     | Var(Ident name)
     | UnaryOp(UnaryOp op, Expr expr)
     | BinaryOp(BinaryOp op, Expr left, Expr right)
     | Assign(Expr target, AssignOp op, Expr value)
     | Cond(Expr condition, Expr thenExpr, Expr elseExpr)
     | Cast(Type type, Expr expr)
     | Call(Expr func, Expr* args)
     | CompoundLiteral(Type type, Initializer init)
     | FieldAccess(Expr expr, Ident field)
     | PtrAccess(Expr expr, Ident field)
     | PostInc(Expr expr)
     | PostDec(Expr expr)
     | SizeOfExpr(Expr expr)
     | SizeOfType(Type type)
     | AlignOf(Type type)
     | Generic(Expr controllingExpr, GenericAssoc* associations)
     attributes(Type type)
```

Operator precedence has completely disappeared. In the grammar, `a + b * c` requires navigating
through `additive_expression → multiplicative_expression` to capture the fact that `*` binds
tighter. In the ASDL, the parser has already resolved precedence by constructing the tree in the
right shape: `BinaryOp(Add, Var("a"), BinaryOp(Mul, Var("b"), Var("c")))`. The structure *is*
the precedence.

The `attributes(Type type)` annotation means every `Expr` node carries a `Type` field. This is
not filled in by the parser — initially it is null — but the typecheck pass populates it. The
ASDL records the *intent* that expressions must be typed, even if that attribute is added by a
later phase.

`BinaryOp` and `UnaryOp` enumerate operator kinds as simple tags:

```
UnaryOp = Address | Deref | Plus | Neg | BitNot | LogNot | PreInc | PreDec

BinaryOp = Mul | Div | Mod
         | Add | Sub
         | LeftShift | RightShift
         | Lt | Gt | Le | Ge
         | Eq | Ne
         | BitAnd | BitXor | BitOr
         | LogAnd | LogOr
```

There is no grammar-level distinction between `<<` and `>>` in this enumeration; they are just
two values of `BinaryOp`. The grammar, by contrast, has separate token names (`LEFT_OP`,
`RIGHT_OP`) and embeds them within `shift_expression` specifically to enforce their precedence.

### Statements

Statements map cleanly to the familiar control-flow constructs:

```
Stmt = ExprStmt(Expr? expr)
     | Compound(DeclOrStmt* items)
     | If(Expr condition, Stmt thenStmt, Stmt? elseStmt)
     | Switch(Expr expr, Stmt body)
     | While(Expr condition, Stmt body)
     | DoWhile(Stmt body, Expr condition)
     | For(ForInit? init, Expr? condition, Expr? update, Stmt body)
     | Goto(Ident label)
     | Continue
     | Break
     | Return(Expr? expr)
     | Labeled(Ident label, Stmt stmt)
     | Case(Expr expr, Stmt stmt)
     | Default(Stmt stmt)
     | Defer(Stmt stmt)
```

`Defer` is not C11. It is vcc's extension, marked as such in all three files: the keyword
`_Defer` (`defer` from `<coro.h>`), a `defer_statement : DEFER statement` alternative of
`statement` in `c11.y`, and this variant. The statement runs when the block holding it is left
— [Coroutines_in_C.md](Coroutines_in_C.md) section 1 is the manual.

`Compound` contains `DeclOrStmt*` — a list of items that can be either declarations or
statements, reflecting C99's rule that declarations and statements can be freely mixed within
a block. `ForInit` captures the similar situation in `for` loop initializers:

```
ForInit = ExprInit(Expr expr)
        | DeclInit(Declaration decl)
```

The dangling-else ambiguity that caused a shift/reduce conflict in the grammar is absent here.
`If` always carries an `Stmt? elseStmt` — the question mark simply records whether the `else`
branch is present, and there is no ambiguity.

### Program structure

The top-level structure is straightforward:

```
Program = Program(ExternalDecl* decls)

ExternalDecl = FunctionDef(DeclSpec specifiers, Declarator declarator,
                           Declaration* decls, Stmt body)
             | Declaration(Declaration decl)
```

`FunctionDef` carries an optional `Declaration* decls` list for K&R-style definitions, where the
parameter types are declared after the parameter name list and before the body.

### Relationship to ast/ast.h

The hand-maintained `ast/ast.h` implements the types described in `c11.asdl` as C structs and
enums. The correspondence is direct but not mechanical: C has no sum types, so each ASDL sum
type becomes an enum tag plus a union in the C struct.

For example, the ASDL `Type` becomes:

```c
typedef enum {
    TYPE_VOID, TYPE_BOOL, TYPE_CHAR, TYPE_SCHAR, TYPE_UCHAR,
    TYPE_SHORT, TYPE_USHORT, TYPE_INT, TYPE_UINT, ...
    TYPE_POINTER, TYPE_ARRAY, TYPE_FUNCTION,
    TYPE_STRUCT, TYPE_UNION, TYPE_ENUM, TYPE_TYPEDEF_NAME, TYPE_ATOMIC
} TypeKind;

struct Type {
    TypeKind kind;
    union {
        struct { Type *target; TypeQualifier *qualifiers; } pointer;
        struct { Type *element; Expr *size; ... } array;
        struct { Type *return_type; Param *params; bool variadic; } function;
        ...
    } u;
    TypeQualifier *qualifiers; /* attributes */
};
```

The `attributes(TypeQualifier* qualifiers)` in the ASDL maps to the `qualifiers` field that
appears at the top level of the `Type` struct, outside the union. The `Ident?` optional fields
become nullable `char *` pointers. The `Field*` and `Param*` list types become linked lists with
a `next` pointer in each node.

The ASDL is not a code generator for `ast/ast.h` — any changes to the AST require editing both
files by hand. The ASDL is the design document; `ast/ast.h` is the implementation. When they
diverge, the ASDL is the authoritative description of intent.

## How the Three Files Fit the Compiler

### Grammar as specification, not code generator

In many compiler toolchains, Lex and Yacc files are the actual source of truth: the build system
runs Flex and Bison, produces `scanner.c` and `parser.c`, and the rest of the compiler is built
on top. This project takes a different approach. The grammar files exist, but no build step reads
them.

The compiler's three layers are all implemented by hand:

| Grammar file | Specifies | Hand-written implementation |
|---|---|---|
| `c11.l` | Token recognition | `scanner/scanner.c` |
| `c11.y` | Syntactic structure | `parser/parser.c` (recursive descent) |
| `c11.asdl` | Abstract syntax | `ast/ast.h` (C structs and enums) |

### Why hand-written

Recursive-descent parsers are easier to extend with good error messages and error recovery than
LALR parsers. When parsing fails mid-declaration, a hand-written function can skip to the next
semicolon and continue, collecting multiple errors in one pass. Error recovery in Yacc grammars
requires special `error` productions and is difficult to tune without detailed knowledge of the
parser's internal state.

Avoiding code generation also removes build-time dependencies on Flex and Bison. The compiler
needs only a C11 compiler and CMake to build; no additional tools are required.

The hand-written scanner and parser also expose their internal state directly in C, making it
easier to integrate with the nametab (`parser/nametab.c`) for typedef disambiguation — the
scanner needs to call back into a table the parser maintains, and that callback is just a
function call in the same process.

### The grammar files as living documentation

The grammar files are valuable precisely because they are separate from the implementation. When
a question arises — "should this C construct parse this way?" — `c11.y` provides a mechanical,
formal answer. When the implementation diverges from the grammar, the discrepancy signals a bug
to investigate.

The files also serve as an authoritative checklist when adding support for a new C11 construct.
The process is:

1. Find the construct in `c11.y` and understand its grammar rule.
2. Find the corresponding ASDL variant in `c11.asdl`.
3. Check whether `ast/ast.h` already has the right node kind.
4. Add recognition to `scanner/` if a new token is needed.
5. Add parsing to `parser/` following the grammar rule as a guide.
6. Update the typecheck pass in `semantic/` and the lowering pass in `translator/`.

The grammar files anchor step 1 and 2 firmly, ensuring the implementation stays aligned with
the C standard even when the code evolves.

### Consistency checking

The `scripts/validate_asdl.py` script uses the `pyasdl` Python library to parse `c11.asdl` and
verify that the file is syntactically valid ASDL. This catches typos and structural errors in
the specification file itself. It does not check consistency between `c11.asdl` and `ast/ast.h`
— that synchronization is maintained by code review and tests — but it ensures the specification
remains a parseable, well-formed ASDL document.
