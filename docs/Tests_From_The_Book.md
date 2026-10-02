# Tests from the book

This article explains, from the ground up, how we test our C compiler using the example
programs from Nora Sandler's book *Writing a C Compiler* (No Starch Press, 2024). It is
written for a reader who is new to compilers **and** new to automated testing. The first
half is a gentle on-ramp — what testing is, how the book's tests are organized, and why
each test belongs to a particular part of our compiler. The second half is the part you
*cannot* get from the book itself: what we actually learned carrying all 20 chapters of its
test corpus onto a 1960s Soviet mainframe, where the clean textbook model bends in
interesting ways.

If you have not yet read the project overview, skim the [README](../README.md) and
[Technical Reference](Technical_Reference.md) first. A friendly companion to this article
is [Learn From This Project](Learn_From_This_Project.md).

## 1. Where the tests come from

The book builds a C compiler one feature at a time over 20 chapters, and it ships with a
large suite of small C programs used to check each step. That suite is published as the
book's companion repository,
[writing-a-c-compiler-tests](https://github.com/nlsandler/writing-a-c-compiler-tests), from
which we draw the programs. It contains roughly **1,600 C programs**, grouped into one
directory per chapter:

```
tests/
├── chapter_1/    A minimal compiler (just `int main(void) { return N; }`)
├── chapter_2/    Unary operators (-, ~)
├── chapter_3/    Binary operators (+, -, *, /, %)
├── ...
├── chapter_18/   Structures and unions
├── chapter_19/   Optimizing the intermediate code
└── chapter_20/   Register allocation
```

Each chapter adds a new slice of the C language. Chapter 1 only knows how to return a
constant; chapter 14 understands pointers; chapter 18 understands `struct`. This gradual
growth is the key to the whole approach: **we never try to test everything at once.** We
take one chapter, turn its programs into tests, make them pass, and move on.

These programs are *source material*. We do not run them straight from the upstream
sources; instead we translate each one into a proper unit test in our own test framework,
which is what the rest of this article is about. We imported the corpus **one chapter at a
time, in order** — chapter 1 first, chapter 20 last — and by the end the suite had grown
from **1,327 tests** (after chapter 1) to **2,548 tests**. The per-chapter history lives in
git, not in a separate checklist.

## 2. What is test-driven development?

A **test** is a small, automated check that answers a yes/no question about your program:
"given this input, does the code do the right thing?" Instead of compiling a program by
hand and squinting at the output, you write the question down once, as code, and let the
computer ask it for you — every time, forever.

**Test-driven development** (TDD) is the discipline of letting those tests lead the work.
The classic loop has three steps, often called *red → green → refactor*:

1. **Red.** Write a test for behavior you want, before (or alongside) the code. Run it; it
   fails (red), because the feature isn't there yet — or it reveals a bug.
2. **Green.** Write the smallest amount of code needed to make the test pass (green).
3. **Refactor.** Now that the test guards the behavior, clean up the code without fear: if
   you break something, a test goes red and tells you immediately.

Two ideas make this powerful for a compiler:

- **A test is executable documentation.** `EXPECT_EQ("2\n", run("int main(void){return 2;}"))`
  says, unambiguously, "this program must produce the output `2`." Nobody has to remember
  it; the test remembers.
- **Tests catch regressions.** A *regression* is when a change quietly breaks something
  that used to work. With thousands of tests, the moment you break chapter 3 while editing
  chapter 9, a red test points at the damage. We saw this for real while writing chapter 1:
  improving the scanner's error messages touched shared code, and re-running the whole
  suite confirmed nothing else broke.

We are not writing the book's compiler from scratch — ours already exists — so we are not
doing pure "test first" TDD. But we are doing the part that matters most: building a dense
safety net of tests, organized so that a failure tells you *exactly where* to look.

## 3. Two kinds of tests: positive and negative

The book's programs come in two flavors, and so do our tests.

A **positive test** uses a *valid* C program and checks that the compiler accepts it and
produces the right answer. Example: `int main(void) { return 2; }` must compile, run, and
yield `2`.

A **negative test** uses a *broken* C program and checks that the compiler **rejects** it —
and, crucially, that it complains in a way a human can understand. Example: `return 1foo;`
is not legal C (a number cannot run straight into letters), so the compiler must stop and
say so clearly.

Negative tests are easy to undervalue, so let us be explicit about why they matter. A
compiler that accepts a broken program is dangerous (it produces nonsense). But a compiler
that rejects a broken program with a *baffling* message is merely annoying — and our
compiler used to do exactly that. Before this work, feeding it a stray `@` produced:

```
Parse error: Expected token 73, got 1 (lexeme: @)
```

"Token 73" means nothing to a human. Writing a negative test forces you to decide what a
*good* message would be, and then improve the compiler until it says it. After the change,
the same input produces:

```
<input>:0: lexical error: invalid character '@'
```

This is why **negative tests are a forcing function for good diagnostics.** That is not just
a slogan — §10 lists the concrete checks the import added because a negative test demanded
them. In our tree we mark negative tests with a `_Neg` suffix in the test name, so positive
and negative tests are easy to tell apart at a glance.

## 4. A quick tour of our compiler

To know *which* part of the compiler should catch a given error, you need a mental map of
the parts. Our compiler is a **pipeline**: source code flows through a series of phases,
each transforming it a little, until machine code comes out the far end.

```
Source (.c)
  → Scanner      groups characters into tokens         (e.g. `return`, `0`, `;`)
  → Parser       groups tokens into a tree (the AST)    (e.g. "a return statement")
  → AST          the program as a structured tree
  → Semantic     checks meaning: types, declarations    (e.g. "is `x` declared? is this assignment legal?")
  → Translator   lowers the tree to simple 3-address code (TAC)
  → Optimizer    simplifies the TAC                      (e.g. folds `2+3` into `5`)
  → BESM-6 backend  turns TAC into machine instructions for the BESM-6 computer
```

Each phase has a single, well-defined job:

- **Scanner** (`scanner/`, also called the *lexer*): reads raw characters and emits
  *tokens* — the words of the language. It rejects characters that cannot begin any token.
- **Parser** (`parser/`): reads tokens and checks they form *grammatically* valid C,
  building an Abstract Syntax Tree (AST). It rejects things like `return int;`.
- **AST** (`ast/`): the tree data structure the parser produces.
- **Semantic analysis** (`semantic/`): walks the tree and checks that it *means* something
  — every name is declared, types match, you don't take the address of a constant, and so
  on. It rejects things like `int *p = 1;`.
- **Translator** (`translator/`): lowers the validated tree into **TAC** (Three-Address
  Code), a simple intermediate language that is easy to optimize and to translate.
- **Optimizer** (`optimize/`): rewrites the TAC to be smaller or faster without changing
  what it computes.
- **BESM-6 backend** (`backend/besm6/`): emits real assembly for the BESM-6, a historic
  Soviet mainframe, which we run on the **Dubna** simulator.

The full diagram and phase status table live in [CLAUDE.md](../CLAUDE.md) and the
[Technical Reference](Technical_Reference.md).

## 5. The directory is a hypothesis, not a verdict

Inside each chapter, the book sorts programs into subdirectories *by what is wrong with
them* (or, for valid programs, that nothing is wrong). Chapter 1 looks like this:

```
tests/chapter_1/
├── valid/            programs that must compile and run
│   ├── return_2.c
│   ├── multi_digit.c
│   └── ...
├── invalid_lex/      programs with a bad *token*  (a scanner error)
│   ├── at_sign.c
│   └── invalid_identifier.c
└── invalid_parse/    programs with bad *grammar*  (a parser error)
    ├── no_semicolon.c
    └── ...
```

Later chapters add more categories as the language grows richer:

- `invalid_lex` — the program contains a character or token that cannot be lexed.
- `invalid_parse` — the tokens are fine, but they don't form a legal sentence.
- `invalid_semantics`, `invalid_types`, `invalid_declarations`, `invalid_labels`,
  `invalid_struct_tags` — the sentence is grammatical but meaningless or ill-typed.
- `valid` — the program is correct; it must compile and produce a specific result.
- `extra_credit` — optional features (compound assignment, `goto`, `switch`, unions). We
  import these right alongside the core tests.

The directory name is a **hypothesis** about which phase should reject the program, and it
rests on a real principle: a broken program fails at the *earliest* phase that can notice
the problem. Walk through three broken programs and watch where each one stops:

- `return 1foo;` — The very first phase, the **scanner**, tries to read `1foo` as a token.
  A number may not be glued to letters, so the scanner gives up immediately. → **scanner**
- `return int;` — The scanner is happy: `return`, `int`, and `;` are all good tokens. But
  the **parser** expects an *expression* after `return`, and `int` is a keyword. → **parser**
- `int *p = 1;` — The scanner and parser are both happy; this is a grammatically correct
  declaration. Only when the **semantic** phase checks *types* does it notice you are
  putting the integer `1` into a pointer variable. → **semantic**

So far, so textbook. Here is the twist that the chapter-1 view hides: **the hypothesis is
about the book's compiler, and ours is not built the same way.** The *earliest phase that
notices* depends on how permissive each phase is — and our parser is deliberately more
permissive than the book's, while our type checker is one unified pass. So programs
**reclassify** across phases all the time. A few real examples from the import:

- **Parser → semantic.** The book treats `2 (- 3)` (`malformed_paren`, ch3) as a *parse*
  error. Our parser accepts a call on *any* postfix expression, so `2(…)` parses fine and
  the **type checker** is what rejects it (you can't call an `int`). The same thing happens
  to `call_non_identifier` and `function_returning_function` (ch9),
  `abstract_function_declarator` (ch14), and others: the grammar lets them through and
  meaning-checking stops them.
- **Semantic → parser.** A `case` label whose value isn't constant (`non_constant_case`,
  ch8; `static_var_case`, ch10) is a *semantic* error in the book. For us a case label is
  parsed as a *constant expression*, so the **parser** rejects it ("Expected constant
  expression") before semantics ever runs. `nested_function_definition` (ch9) and
  `cast_to_array_type_3` (ch15, `long(([2])[3])` → "Empty type specifier list") move the
  same way.
- **Valid → negative.** This is the most surprising one. We made a permanent design
  decision: **no identifier shadowing** — an inner block may not redeclare a name visible
  in an enclosing scope (see the design notes in the
  [Technical Reference](Technical_Reference.md)). The book, following standard C, treats
  shadowing as perfectly *valid*. So a whole class of the book's `valid/` programs become
  **semantic negatives** for us, dying with "Duplicate variable declaration." In chapter 7
  (which is *about* shadowing) **10 of 16** "valid" programs flip this way; it recurs in
  chapters 8, 9, 10, 18, and 20.

The practical upshot when importing a chapter: the book's directory tells you *where to look
first*, not where the test will end up. Part of the work is running each program through
`parse`/`lower` and seeing which phase actually speaks — then filing the test next to that
phase.

## 6. Matching tests to compiler phases

With that caveat in hand, the mapping is still the backbone of the whole effort:

| Book directory | Compiler phase | Our test file | What the test asserts |
|---|---|---|---|
| `invalid_lex` | scanner | `scanner/test/chapterNN_tests.cpp` | aborts with a lexical-error message |
| `invalid_parse` | parser | `parser/test/chapterNN_tests.cpp` | aborts with a parse-error message |
| `invalid_semantics`, `invalid_types`, `invalid_declarations`, `invalid_labels`, `invalid_struct_tags` | semantic | `semantic/test/chapterNN_tests.cpp` | aborts with a semantic-error message |
| `valid` (and `extra_credit`, `libraries`) | every backend | `backend/common/test/book/chapterNN_tests.cpp` | compiles, runs, and prints the expected result |
| chapter 19 (`constant_folding`, `copy_propagation`, …) | optimizer | `optimize/test/chapter19_tests.cpp` | the TAC is simplified as expected |

This is a beautiful correspondence: **our source tree already has one directory per phase**
(`scanner/`, `parser/`, `semantic/`, `translator/`, `optimize/`, `backend/besm6/`), and the
book's tests already sort themselves into those same buckets. Importing a chapter is mostly
a matter of carrying each program to its rightful home — adjusted, per §5, for the cases
where *our* compiler catches a thing in a different phase than the book does.

## 7. How we run the positive tests

Negative tests are about *rejecting* bad programs, which any phase can do on its own. But a
positive test must *run* a valid program and check its answer — and to run a program you
need a complete compiler with a real backend. We have one: the **BESM-6 backend** plus the
**Dubna** simulator. So our positive tests genuinely compile each program to BESM-6 machine
code and execute it.

There is one wrinkle. On our BESM-6 runtime, execution starts at a function called
`program`, not `main`, and we observe a program by what it *prints*, not by an exit code.
The book's valid programs, however, are written as `int main(void)` and are judged by the
value `main` returns. To bridge the two, we wrap each program with a tiny `program` that
calls `main` and prints its return value. That wrapper lives in
[backend/besm6/test/codegen_test.h](../backend/besm6/test/codegen_test.h):

```c
// Wrap a book program so program() prints `main()`'s return value as "%d\n".
inline std::string WrapMain(const std::string &program)
{
    return "int printf(const char *format, ...);\n" + program +
           "\nvoid program(void) { printf(\"%d\\n\", main()); }\n";
}
```

So a book program that ends `return 2;` becomes a program that *prints* `2`, and our test
simply checks that the output is `"2\n"`. This one trick works for the whole corpus: simple
programs whose return value is the point, and the book's "self-checking" programs (which
return `0` on success and a nonzero code on the first failed check) alike.

Two details make the comparison honest:

- **Where the expected value comes from.** We do not hand-type the expected number. We take
  the *same wrapped source*, compile it with the host `cc`, and use its stdout as the
  expectation. Comparing printed output (not an exit code) sidesteps the `mod 256`
  truncation a shell exit status would impose on a return value like `300`.
- **Multi-file `libraries` tests.** Some book tests split a program across a client file and
  a library file. We have no linker in this pipeline, so we **concatenate them into one
  translation unit, client first**, and compile that.

## 8. When the target disagrees with the book

Here is the single most important thing the book cannot teach you, because the book targets
x86 and we target a machine from 1968. **A perfectly correct C program can legitimately fail
on BESM-6 — not because our compiler is wrong, but because the two machines compute
different answers to the same C.** A positive run test only passes when the backend's
arithmetic *agrees* with the model the book's program was written against. Where they
disagree, the test cannot pass, and pretending otherwise would be dishonest.

The disagreements all trace back to a handful of architectural facts. For the full story see
[Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md); the essentials:

| Aspect | BESM-6 | The book's x86 model |
|---|---|---|
| `int`, `long` | the **same** 41-bit signed word (±2⁴⁰ ≈ ±1.1×10¹²) | 32-bit `int`, 64-bit `long` |
| `unsigned` (all widths) | one 48-bit word (0 … 2⁴⁸−1) | 32-bit / 64-bit |
| floating point | one 48-bit format for `float`/`double`/`long double`: 7-bit exponent (~10⁻¹⁹ … ~9.2×10¹⁸), 40-bit mantissa (~12 digits), **no NaN, infinity, negative zero, or subnormals** | IEEE-754 binary32 / binary64 |
| addressing | **word**-addressed (a pointer is a word index) | byte-addressed |
| identifiers | Madlen assembler truncates to **8 characters** | effectively unlimited |
| output charset | lowercase Latin folds to **Cyrillic** (GOST) | ASCII |
| `>>` of a negative | **logical** (no sign extension) | arithmetic (implementation-defined) |
| runtime | hosted libc subset (stdio/string/math helpers); allocator on the Unix path only, no libm transcendentals | full libc + libm |

Now the worked examples — each one a real program from the corpus.

**Truncation that doesn't happen** (chapter 11, `truncate` / `convert_by_assignment`). The
program assigns a 64-bit `long` to a 32-bit `int` and self-checks that the high bits were
*lost*. On BESM-6 `int` and `long` are the *same* 41-bit word, so nothing is truncated, the
value survives intact, the self-check sees the "wrong" answer and returns a failure code.
The codegen is flawless; the *premise* of the test does not exist on this machine. Disabled.
A cluster of chapter-11/12 programs (`switch_int`, `convert_function_arguments`,
`compound_assign_to_int`, …) fail for exactly this reason.

**Unsigned width** (chapter 12). The chapter exists to prove an x86 compiler distinguishes a
32-bit `unsigned int` (wraps at 2³²) from a 64-bit `unsigned long` (wraps at 2⁶⁴). BESM-6
has a *single* 48-bit unsigned word, so `-1u`, an `unsigned int` incremented past 2³²−1, and
the various narrowing casts simply don't behave as the program expects. The four programs
whose values happen to stay in range and don't depend on the wrap point run and pass; the
other 25 are disabled.

**No NaN, no infinity** (chapter 13). `nan`, `infinity`, `negative_zero`, and
`subnormal_not_zero` exercise IEEE corner cases the BESM-6 float format does not have — there
is no bit pattern for NaN, and `isnan` would need libm besides. Separately, `return_double`
returns `1234e75`, whose exponent blows past the format's ~9.2×10¹⁸ ceiling. Different
reasons, same outcome: disabled.

**The rare program we keep with a *different* expected value** (chapter 10,
`bitwise_ops_file_scope_vars`). Right-shifting a *negative* integer is
implementation-defined in C11 (§6.5.7p5): x86 shifts arithmetically, BESM-6 shifts
logically. This program is *not* self-checking — it just computes a value and returns it. So
we keep it **enabled** and set the expected value to the genuine BESM-6 result (`2`), not the
x86 result (`0`). That is legitimate: the C standard blesses both.

The contrast with the truncation case above is the whole judgment call, and it is worth
stating plainly. When a program merely *computes and returns* a value whose C semantics are
implementation-defined, we keep it and expect the BESM-6 answer. When a program *self-checks*
and returns a pass/fail code, a BESM-6-tuned expectation would just be encoding the
program's own failure code — a green checkmark that secretly means "this test failed." That
is worse than useless, so those programs are disabled, not "fixed" with a doctored number.
**Disabling is the honest call.**

**Mechanical target limits.** Three smaller constraints disable programs that are otherwise
perfectly in range:

- *8-character identifiers.* Madlen truncates names to 8 chars, so `one_hundred` and
  `one_hundred_ulong` (chapter 12 `comparisons`) both become `ONE*HUND` and collide
  ("twice-described identifier"); chapter 20's `glob_four` / `glob_fourteen` collide on
  `glob_fou` and had to be renamed to `gr0…gr14` to enable the test.

  This means **a book test must be updated so that its *external* names — functions and
  file-scope globals, anything that becomes a Madlen label — are unique within their first 8
  characters** (after the backend's `_`/`$`/`%`→`*`/`/` substitution; see
  [Madlen.md §3](../backend/besm6/Madlen.md)). The collision is silent: the assembler/loader merges the two
  names and the later definition wins, so calls to one C name run the other's body — a wrong
  result with no compile- or link-time error. Rename the offending helpers to short distinct
  stems rather than disabling the test when that is the only obstacle: chapter 15's
  `pointer_diff`, for example, was enabled by shortening `get_multidim_ptr_diff` /
  `get_multidim_ptr_diff_2` (both → `get*mult`) to `pdiff_m` / `pdiff_m2`. Block-scope locals
  are frame slots, not labels, so they need no such care.
- *Output charset.* The runtime renders lowercase Latin as Cyrillic, so the book's
  `hello_world` prints uppercase letters and our expected strings use UPPERCASE ASCII.
- *Simulator time budget.* Chapter 8's `empty_loop_body` is correct codegen but spins ~430
  million iterations — over a minute on Dubna, past the 10-second ctest timeout — so it is
  disabled for speed, not correctness.

## 9. A field guide to `DISABLED_`

Sometimes the book exercises a feature our backend cannot handle *yet*, or — far more often,
per §8 — a feature this machine simply does not have. We do not let that block a chapter, and
we do not quietly drop the test. GoogleTest lets you prefix a test name with `DISABLED_` to
register it but skip it:

```cpp
TEST_F(CodegenTest, DISABLED_Chapter13_Nan) { /* BESM-6 FP has no NaN */ }
```

The test is visible (reported as skipped) so we remember it, but it does not fail the build.
Now that the run programs are shared by every backend, a program one target cannot run goes on
that backend's skip list (`BookTest` in its `test/book_test.h`) instead.
Every disabled test carries a **one-line reason**. Across 20 chapters those reasons settle
into a small, recurring taxonomy — learn these eight and you can predict why almost any book
program is disabled:

| # | Category | Representative example |
|---|---|---|
| B | Value exceeds BESM-6 range (41-bit signed / 48-bit unsigned / FP exponent) | ch11 `simple` (±(2⁶³−1)), ch13 `return_double` |
| C | Relies on x86 32/64-bit truncation or wraparound | ch11 `truncate`, ch12 `switch_uint` |
| D | Needs runtime absent from the Madlen `libc.bin`: libm transcendentals, or the Unix-only allocator (`malloc`/`free`) | ch17 `void_pointer/*`, ch13 `standard_library_call` |
| E | Forbidden by the no-shadowing design rule | ch7/8/9/10/18/20 shadowing programs |
| F | 8-character Madlen identifier collision | ch12 `comparisons`, ch20 `briggs_xmm_k_value` |
| G | Output charset folds lowercase Latin to Cyrillic | ch16 `write_to_array` |
| H | Loop runs past the 10s ctest timeout | ch8 `empty_loop_body`, ch9 `test_for_memory_leaks` |
| I | Assumes x86 byte addressing / page boundaries / `.s` helpers | ch14 `pointer_int_casts`, `push_arg_on_page_boundary` |

The discipline behind this is worth making explicit, because it is the difference between a
test suite you can trust and one you cannot:

- **One faithful test per book program.** We transcribe every program, even the ones we know
  we must disable — so the corpus stays complete and a future backend improvement has a test
  already waiting to be flipped on.
- **Disable, don't hide.** A `DISABLED_` test with a reason is a tracked gap. A silently
  omitted program is forgotten knowledge.
- **Never encode a meaningless number.** As §8 argued, a self-checking program must never be
  "made to pass" with a BESM-6-tuned expectation that is really its own failure code.

## 10. Negative tests buy diagnostics — the receipts

Section 3 claimed that writing a negative test forces a better error message. That is not
aspirational; the import added roughly thirty real checks to the compiler, each because a
book program slipped through silently and a negative test demanded a diagnostic. A sampler,
by phase:

- **Scanner.** Integer/float *suffix* validation, so `0lL` and `0LLL` are rejected while
  `10ULL` and `1.0f` still lex (ch11); a *missing exponent* (`30.e`, `24e-`) now errors
  instead of mis-tokenizing (ch13); unknown *escape sequences* (`'\y'`, `"foo\ybar"`) are
  rejected — `scan_string` previously validated escapes not at all (ch16).
- **Parser.** Rejecting more than one storage-class specifier (`static extern`) and a
  storage class on a parameter (ch10); rejecting a duplicate or conflicting *signedness*
  specifier (`signed unsigned`), which had silently last-wins (ch12).
- **Semantic.** A function returning a function or an array; duplicate parameter names; an
  initializer on a function declaration; a function designator as the operand of `++`
  (ch9); the whole `void`/incomplete-type family — value-less `return` in a non-`void`
  function, `++` on a `void*`, relational/equality comparisons involving `void` (ch17); a
  non-null integer as a static pointer initializer (ch14).

Each of these is a message a human can now act on, in place of an accept-and-miscompile or a
"Token 73." The negative tests didn't just *document* the compiler's behavior; they *grew*
it.

## 11. Anatomy of a test in our framework

We write tests with [GoogleTest](https://github.com/google/googletest), a widely used C++
testing library. You only need to recognize a handful of pieces:

- `TEST(SuiteName, TestName) { ... }` defines a test. `TEST_F(FixtureName, TestName)` does
  the same but gives the test access to a shared *fixture* (setup/teardown helpers).
- `EXPECT_EQ(expected, actual)` checks two values are equal; if not, the test fails but
  keeps going.
- `EXPECT_DEATH(statement, "regex")` checks that running `statement` makes the process
  **abort**, and that its error output matches the regular expression. This is exactly what
  we need for negative tests: our compiler reports a fatal error and exits, so "did it die
  with the right message?" is the question to ask.

Let us read one real test of each kind, straight from the committed chapter-1 files.

**A scanner negative test** (from [scanner/test/chapter1_tests.cpp](../scanner/test/chapter1_tests.cpp)).
The helper `LexToEnd` runs the scanner over a string until end-of-input; on a bad token the
scanner aborts, and `EXPECT_DEATH` catches it:

```cpp
// return 0@1; — '@' is not part of any C token outside a literal.
TEST(ScannerChapter1, AtSign_Neg)
{
    EXPECT_DEATH(LexToEnd("int main(void) {\n    return 0@1;\n}\n"),
                 "invalid character '@'");
}
```

`LexToEnd` itself is a small shared helper in
[scanner/test/scan_fixture.h](../scanner/test/scan_fixture.h) — it writes the source to a temporary
file, points the scanner at it, and pulls tokens until the end. We factor such helpers into
a header so every chapter's scanner tests can reuse them.

**A parser negative test** (from [parser/test/chapter1_tests.cpp](../parser/test/chapter1_tests.cpp)).
Here the `ParserTest` fixture provides `parse(CreateTempFile(...))`:

```cpp
// Missing semicolon after the return value.
TEST_F(ParserTest, Chapter1_NoSemicolon_Neg)
{
    EXPECT_DEATH(parse(CreateTempFile("int main (void) {\n    return 0\n}\n")),
                 "expected ';', got '\\}'");
}
```

Notice the assertion is the *improved* message we designed while writing the test:
`expected ';', got '}'`, not `Expected token 73`. The test and the diagnostic were built
together — that is the forcing function from §3 in action. (The `\\}` is just the regex
escape for a literal `}`.)

**A positive run test** (from [backend/common/test/book/chapter1_tests.cpp](../backend/common/test/book/chapter1_tests.cpp)).
The `CodegenTest` fixture's `CompileAndRun` compiles the source, runs it on Dubna, and
returns whatever it printed:

```cpp
// return 2;
TEST_F(CodegenTest, Chapter1_Return2)
{
    EXPECT_EQ("2\n", CompileAndRun(WrapMain("int main(void) { return 2; }")));
}
```

Read it aloud: "wrap `int main(void){return 2;}` so it prints `main()`'s result, compile
and run it, and expect the output `2`." That is a complete, end-to-end test of the entire
compiler in three lines.

## 12. Naming and file organization

A few simple conventions keep the growing suite navigable:

- **One file per chapter, per component.** Chapter 5's parser tests go in
  `parser/test/chapter5_tests.cpp`; its semantic tests in `semantic/test/chapter5_tests.cpp`; its
  runnable programs in `backend/common/test/book/chapter5_tests.cpp`. The file's directory tells you
  the phase; the filename tells you the chapter.
- **`_Neg` marks negative tests.** A name ending in `_Neg` is a "this must be rejected"
  test; everything else is a positive test.
- **Tests live next to the code they exercise.** This is a long-standing rule in this
  project (see the Tests section of the [Technical Reference](Technical_Reference.md)): the
  scanner's tests are in `scanner/`, the parser's in `parser/`, and so on. The book's
  directory layout maps onto ours almost perfectly, which — adjusting for the
  reclassifications of §5 — is why the import is largely mechanical.

Each new chapter file is added to its component's test executable in the relevant
`CMakeLists.txt` (for example `parser/CMakeLists.txt` lists the chapter sources in the same
`parser-tests` binary as the everyday unit tests), so it is picked up by `make test`
(see §14).

## 13. The incremental workflow

We imported the corpus **one chapter at a time, in order**, because each chapter depends only
on features from earlier chapters. A chapter is considered *done* when:

1. its new test files build, and
2. the **entire** test suite still passes — `make test` runs the unit tests and the chapter
   tests together.

Two pieces of discipline kept the import honest as it scaled to 2,548 tests, and both are
worth carrying into any test-import work of your own:

- **Transcribe faithfully, then disable what the target can't do.** Every program becomes a
  test. The ones the machine cannot run become `DISABLED_` with a one-line reason (§9),
  never silently dropped — so the suite is a complete census of the corpus and the gaps are
  visible, not lost.
- **Let the failures teach you.** A genuinely red run test means a backend bug, and the
  import surfaced real ones (multi-dimensional array decay, pointer-to-array scaling, signed
  complement corrupting the FP exponent field, union sizing, …). The `DISABLED_` reason
  forces you to *classify* each gap, which is how you tell a real bug apart from a
  target-semantics mismatch (§8). The first you fix; the second you document.

## 14. Running the tests

The chapter tests are compiled **into the regular per-module test binaries** — the chapter
sources sit in the same `add_executable(<module>-tests …)` as the unit tests. So
`parser-tests` holds the parser chapter tests, `besm-tests` holds the BESM-6 run tests, and
so on. All of the test executables are `EXCLUDE_FROM_ALL`, so a plain `make` builds only the
compiler and runtime; one target builds and runs everything:

```sh
make run                  # builds every test binary, then runs all tests (unit + chapter)
```

`make run` builds `all` (compiler, runtime, and every test binary) and runs
`ctest --test-dir build` over the whole suite. There is no separate book target or ctest
label any more.

To run a single component or a single test while developing, first make sure the test
binaries are built (`make` or `cmake --build build`), then
**run from inside the `build/` directory**, not from the repository root:

```sh
ctest --test-dir build -R Chapter1                    # every chapter-1 test, anywhere
cd build/parser        && ./parser-tests              # parser unit + chapter tests
cd build/scanner       && ./scanner-tests
cd build/backend/besm6 && ./besm-tests                # the run tests (need libc.bin here)
```

Why the `cd`? Some fixtures write small temporary files into the *current directory* while
they run. If you launch a test binary from the repository root, those scratch files litter
your source tree; launched from inside `build/`, they stay out of the way. The BESM-6 run
tests have an extra reason: they link the runtime library `libc.bin`, which is built in
`build/backend/besm6`, so they must run from there. (Also: don't run two `besm-tests`
processes at once — they share scratch filenames.)

To run just one test by name:

```sh
cd build/backend/besm6 && ./besm-tests --gtest_filter='CodegenTest.Chapter1_Return2'
```

## 15. Takeaways

If you remember three things from this article, make them these:

1. **Tests are how you make change safe.** A dense, automated suite turns "I hope I didn't
   break anything" into "the computer just confirmed I didn't." That is what lets a compiler
   grow without rotting.
2. **An error is a classifier — but the classifier can disagree with the book.** The kind of
   mistake usually tells you which phase should catch it (bad token → scanner, bad grammar →
   parser, bad meaning → semantic). Yet because our parser is more permissive and our type
   checker is unified, many programs reclassify across phases — and our no-shadowing rule
   turns some *valid* programs into negatives. The directory is a hypothesis, not a verdict.
3. **The target decides which valid programs can even run.** BESM-6's 41-bit integers,
   48-bit unsigneds, NaN-free floats, and word addressing mean a *correct* C program can
   legitimately compute a different answer than x86 — so a large, well-understood slice of
   the corpus is `DISABLED_` on purpose, each with a one-line reason. Honest disabling beats
   a doctored green checkmark.

From here, the natural next steps are to browse the committed chapter test files named
throughout this article (`scanner/test/chapter1_tests.cpp`, `backend/common/test/book/chapter13_tests.cpp`,
and their siblings), and to consult [Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md)
and the [Technical Reference](Technical_Reference.md) for the full phase-by-phase design and
the target's data model.
