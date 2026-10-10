# Tests from the book

This article explains, from the ground up, how we test our C compiler using the example
programs from Nora Sandler's book *Writing a C Compiler* (No Starch Press, 2024). It is
written for a reader who is new to compilers **and** new to automated testing. The first
half is a gentle on-ramp — what testing is, how the book's tests are organized, and why
each test belongs to a particular part of our compiler. The second half is the part you
*cannot* get from the book itself: how one set of run tests serves every backend, including
a target whose data model differs from the book's.

If you have not yet read the project overview, skim the [README](../README.md) and
[Technical Reference](Technical_Reference.md) first. The RISC-V backend, which the run-test
examples below use, is described in [Riscv_Backend.md](Riscv_Backend.md).

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
t.c:3:5: error: invalid character '@'
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
  → Backend      turns TAC into assembly for one target  (RISC-V or BESM-6)
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
- **Backends** (`backend/`): emit real assembly for a target machine. There are two. The
  **RISC-V** backend (`backend/riscv/`) produces RV64 code that we run on the
  `qemu-system-riscv64` emulator. The **BESM-6** backend (`backend/besm6/`) produces code for
  a historic Soviet mainframe, which we run on a simulator.

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
| chapter 19 (`constant_folding`, `copy_propagation`, …) | optimizer | `optimize/test/chapter19_tests1.cpp` … `chapter19_tests4.cpp` | the TAC is simplified as expected |

This is a beautiful correspondence: **our source tree already has one directory per phase**
(`scanner/`, `parser/`, `semantic/`, `translator/`, `optimize/`, `backend/`), and the
book's tests already sort themselves into those same buckets. Importing a chapter is mostly
a matter of carrying each program to its rightful home — adjusted, per §5, for the cases
where *our* compiler catches a thing in a different phase than the book does.

## 7. How we run the positive tests

Negative tests are about *rejecting* bad programs, which any phase can do on its own. But a
positive test must *run* a valid program and check its answer — and to run a program you
need a complete compiler with a real backend. We have two, and the run tests are written
once for both.

**One suite, every backend.** The book's valid programs live in
[backend/common/test/book/](../backend/common/test/book/) (`chapter1_tests.cpp` …
`chapter20_tests.cpp`; chapter 18 is split over four files). They do not belong to any one
target. CMake collects them into `BOOK_TEST_SOURCES`
([backend/common/CMakeLists.txt](../backend/common/CMakeLists.txt)), and each backend
compiles that same list into its own test executable — `riscv-tests` and `besm-tests`.
Every test is written against a fixture called `BookTest`, and each backend defines its
own `BookTest` in its own `test/book_test.h`. The `#include "book_test.h"` at the top of a
chapter file therefore picks up a different fixture in each executable: the same
`TEST_F(BookTest, Chapter1_Return2)` runs on RISC-V in one binary and on BESM-6 in the
other.

**How a RISC-V run works.** The RISC-V fixture,
[backend/riscv/test/book_test.h](../backend/riscv/test/book_test.h), derives from the
backend's general run fixture `RiscvTest` in
[backend/riscv/test/riscv_test.h](../backend/riscv/test/riscv_test.h). Its
`CompileAndRunBook(src)` does the whole trip in-process and through real tools:

1. compile the C source through our pipeline (parse → typecheck → translate → optimize →
   `riscv_codegen`) to RISC-V assembly;
2. assemble it with clang (`--target=riscv64 -march=rv64imfd -mabi=lp64d`);
3. link it with `ld.lld` against our runtime (`crt0-status.o`, `libc.a`) and the qemu `virt`
   linker script;
4. boot the executable on bare-metal `qemu-system-riscv64 -M virt -bios none`, with the
   program's UART output captured as its stdout (with a 5-second limit per run).

The book's programs are judged by the value `main` returns, but a test compares *printed
output*. The bridge is a startup object: `crt0-status.o` is our ordinary `crt0` built with
`-DPRINT_STATUS` ([libc/riscv64/crt0.S](../libc/riscv64/crt0.S)), which calls `main` and then
prints its return value as `"%d\n"` before exiting. So a book program that ends
`return 2;` prints `2`, and the test checks that the output is `"2\n"`. This one trick works
for the whole corpus: simple programs whose return value is the point, and the book's
"self-checking" programs (which return `0` on success and a nonzero code on the first failed
check) alike. Printing the value, rather than reading an exit code, also sidesteps the
`mod 256` truncation an exit status would impose on a return value like `300`.

**A second opinion from clang.** The expected string in each test is a literal, written
into the test. On RISC-V that is not the only check: the fixture's `CompileAndRunBook`
also compiles the *same source* with clang (`-O0`, against our own target headers), links
it with the same `crt0-status.o` and runs it on qemu too. The test then requires our output
to equal clang's (`"differs from clang"`) and our exit status to equal clang's. clang is
an independent, mature RISC-V compiler, so every book program is a differential test as
well as a unit test: if our output and the expected literal agree but clang disagrees, the
expectation itself is suspect.

**When the tools are missing.** The run tests need the RISC-V binutils (or clang and
`ld.lld`) and `qemu-system-riscv64` (found by CMake through
[scripts/CrossTools.cmake](../scripts/CrossTools.cmake); see the prerequisites in the
[README](../README.md)). The fixture's `SetUp` calls `SKIP_IF_NO_RISCV_TOOLS()`, so on
a machine without them every book test reports *skipped* rather than failed, and
`make run` stays green. Without clang, the comparison with clang's build is left out and
each program is checked against the book's own result only.

**Multi-file `libraries` tests.** Some book tests split a program across a client file and
a library file. The fixture compiles a single source, so we **concatenate them into one
translation unit, client first** (for example `Chapter9_LibraryAddition`).

**The same suite on BESM-6.** The second backend's fixture,
[backend/besm6/test/book_test.h](../backend/besm6/test/book_test.h), derives from
`CodegenTest` and implements `CompileAndRunBook` with the BESM-6 Unix toolchain: our
`genbesm` assembly is assembled by `b6as`, linked by `b6ld`, and run under the `b6sim`
simulator with `--status`, which likewise appends `main`'s result to the output. There is
no clang to compare against on that machine, so only the expected literal is checked.

## 8. When the target disagrees with the book

The book targets x86-64. RISC-V's LP64 data model is the same as the book's — 32-bit `int`,
64-bit `long` and pointers, IEEE-754 floating point, byte addressing — so on RISC-V every one
of the shared programs runs, and its output agrees with clang's.

A target with a different data model teaches the one thing the book cannot: **a perfectly
correct C program can legitimately compute a different answer on another machine — not
because the compiler is wrong, but because the two machines compute different answers to the
same C.** A run test only passes when the backend's arithmetic *agrees* with the model the
program was written against. The second backend, BESM-6, is such a machine — one 41-bit word
for both `int` and `long`, word addressing, a non-IEEE float format (see
[Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md)) — and the
suite settles each disagreement in one of three ways.

**Width-dependent programs: a generic version, plus a target version.** Chapter 11 assigns a
64-bit `long` to a 32-bit `int` and self-checks that the high bits were *lost*
(`Chapter11_Truncate`). Where `int` and `long` are the same width nothing is truncated and
the self-check fails — the codegen is flawless; the *premise* of the test does not exist on
that machine. Such programs stay in the shared suite in their generic LP64 form, as in the
book; a target that cannot honour the premise skips them (§9) and runs its own rewritten
versions instead (for BESM-6,
[book_besm6_tests.cpp](../backend/besm6/test/book_besm6_tests.cpp)).

**Implementation-defined results: one test, a per-target expectation.** Right-shifting a
*negative* integer is implementation-defined in C11 (§6.5.7p5): RISC-V shifts
arithmetically, some machines logically. `Chapter3_BitwiseShiftrNegative` just computes
`-5 >> 30` and returns it, so it stays in the shared suite with a per-target expectation;
the fixture's `IsTarget` tells the test which backend it is running on:

```cpp
EXPECT_EQ(IsTarget("besm6") ? "2047\n" : "-1\n",
          CompileAndRunBook("int main(void) { return -5 >> 30; }"));
```

**Programs with no analogue: removed.** The IEEE corner-case programs of chapters 13 and 19
(`nan`, `infinity`, `negative_zero`, `subnormal_not_zero`) are not in the suite.

The contrast between the first two cases is the whole judgment call. When a program merely
*computes and returns* a value whose C semantics are implementation-defined, we keep it and
expect each target's answer. When a program *self-checks* and returns a pass/fail code, a
target-tuned expectation would just be encoding the program's own failure code — a green
checkmark that secretly means "this test failed." So such a program is never "fixed" with a
doctored number: the target either runs a version of the program that is true to its data
model, or skips it.

## 9. Skip lists

Each backend's `BookTest` has a **skip list** — an array of `{ test name, reason }` pairs
that its `SetUp` hands to `SkipIfListed` (from
[backend/common/test/backend_test.h](../backend/common/test/backend_test.h)), which calls
`GTEST_SKIP()` with the reason when the current test is on it. A skipped test is still
reported, with its reason, so nothing is silently lost. (A name prefix such as GoogleTest's
`DISABLED_` would not do: a program can be fine on one target and impossible on another.)

- **RISC-V has no skip list.** Its `BookTest` skips only when the tools are missing; all of
  the shared programs run on qemu and agree with clang.
- **BESM-6 skips the width-dependent programs** and runs its own versions of them under a
  separate fixture (`Besm6BookTest`).
- **AVR compares every program with clang**, which shares its 16-bit `int`, so a program
  whose answer changes with the width still has a reference; its book fixture judges the
  output and exit status by clang's, not by the book's expectation. It skips only what
  cannot run there: shifts by 16 or more and other undefined behavior with a 16-bit
  `int`, case values that collide in a 32-bit `long`, programs too large for 8 KB of
  SRAM or a 16-bit `size_t`, and the slowest under qemu; each reason is in
  [backend/avr/test/book_test.h](../backend/avr/test/book_test.h).
- **MSP430 compares every program with GCC's build**, made by `msp430-elf-gcc` with
  newlib, and with clang's. It skips what fails in GCC's build too, or is undefined with a
  16-bit `int` and comes out differently there: case values that collide in a 32-bit
  `long`, a 16-bit `size_t`, arrays too large for 15.5 KB of RAM, the slowest under the
  cycle limit, and programs that read garbage at 16 bits; each reason is in
  [backend/msp430/test/book_test.h](../backend/msp430/test/book_test.h).

The discipline behind this is worth making explicit, because it is the difference between a
test suite you can trust and one you cannot:

- **One faithful test per book program.** Every program is transcribed in its generic form,
  even when some target cannot run it — so the corpus stays complete and a new backend has a
  test already waiting for it.
- **Skip, don't hide.** A skipped test with a reason is a tracked gap. A silently omitted
  program is forgotten knowledge.
- **Never encode a meaningless number.** As §8 argued, a self-checking program must never be
  "made to pass" with a target-tuned expectation that is really its own failure code.

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
The `BookTest` fixture's `CompileAndRunBook` compiles the source, runs it, and returns what
it printed followed by `main`'s return value:

```cpp
// return 2;
TEST_F(BookTest, Chapter1_Return2)
{
    EXPECT_EQ("2\n", CompileAndRunBook("int main(void) { return 2; }"));
}
```

Read it aloud: "compile `int main(void){return 2;}`, run it, and expect the output `2`."
That is a complete, end-to-end test of the entire compiler in three lines. In `riscv-tests`
those three lines also assemble and link with real RISC-V tools, boot the program on qemu,
and check that clang's build of the same program prints the same thing (§7).

## 12. Naming and file organization

A few simple conventions keep the growing suite navigable:

- **One file per chapter, per component.** Chapter 5's parser tests go in
  `parser/test/chapter5_tests.cpp`; its semantic tests in `semantic/test/chapter5_tests.cpp`; its
  runnable programs in `backend/common/test/book/chapter5_tests.cpp`. The file's directory tells you
  the phase; the filename tells you the chapter. (A very large chapter is split into
  numbered files, such as `chapter18_tests1.cpp` … `chapter18_tests4.cpp`.)
- **`_Neg` marks negative tests.** A name ending in `_Neg` is a "this must be rejected"
  test; everything else is a positive test.
- **Tests live next to the code they exercise.** This is a long-standing rule in this
  project (see the Tests section of the [Technical Reference](Technical_Reference.md)): the
  scanner's tests are in `scanner/`, the parser's in `parser/`, and so on. The run programs
  are the one shared exception: they exercise *every* backend, so they live in
  `backend/common/` and each backend's test executable compiles them. The book's directory
  layout maps onto ours almost perfectly, which — adjusting for the reclassifications of
  §5 — is why the import is largely mechanical.

Each new chapter file is added to its component's test executable in the relevant
`CMakeLists.txt` (for example `parser/CMakeLists.txt` lists the chapter sources in the same
`parser-tests` binary as the everyday unit tests). The run programs need no such edit:
[backend/common/CMakeLists.txt](../backend/common/CMakeLists.txt) globs
`test/book/chapter*_tests*.cpp`, and both `riscv-tests` and `besm-tests` compile the result.

## 13. The incremental workflow

We imported the corpus **one chapter at a time, in order**, because each chapter depends only
on features from earlier chapters. A chapter is considered *done* when:

1. its new test files build, and
2. the **entire** test suite still passes — `make run` runs the unit tests and the chapter
   tests together.

Two pieces of discipline kept the import honest as it scaled to 2,548 tests, and both are
worth carrying into any test-import work of your own:

- **Transcribe faithfully, then mark what the target can't do.** Every program becomes a
  test. The ones a machine cannot run were marked with a one-line reason — `DISABLED_` then,
  a skip-list entry now (§9) — never silently dropped, so the suite is a complete census of
  the corpus and the gaps are visible, not lost.
- **Let the failures teach you.** A genuinely red run test means a backend bug, and the
  import surfaced real ones (multi-dimensional array decay, pointer-to-array scaling, signed
  complement corrupting the FP exponent field, union sizing, …). The one-line reason forces
  you to *classify* each gap, which is how you tell a real bug apart from a target-semantics
  mismatch (§8). The first you fix; the second you document.

The same suite then paid for itself a second time. When the RISC-V backend was written, the
book programs were its acceptance test, chapter by chapter, from the first `return 2;` to
structures and unions — and, once the clang comparison was added, a differential test too.

## 14. Running the tests

The chapter tests are compiled **into the regular per-module test binaries** — the chapter
sources sit in the same `add_executable(<module>-tests …)` as the unit tests. So
`parser-tests` holds the parser chapter tests, `riscv-tests` and `besm-tests` each hold the
whole set of run programs, and so on. A plain `make` builds the compiler, the runtime *and*
every test binary; `make test` does the same without running anything; one target builds
and runs everything:

```sh
make run                  # builds every test binary, then runs all tests (unit + chapter)
```

`make run` runs `ctest --test-dir build` over the whole suite. There is no separate book
target or ctest label. ctest gives each test 10 seconds.

To run a single component or a single test while developing, run the binary directly. Every
test binary `chdir()`s into its own build directory at startup, so it can be launched from
anywhere — its scratch files (for a RISC-V run: `<TestName>.s`, `.o`, `.elf`, `.out`, and
the clang build's files beside them) land in `build/…`, not in your source tree:

```sh
ctest --test-dir build -R Chapter1                              # every chapter-1 test, anywhere
./build/parser/parser-tests                                     # parser unit + chapter tests
./build/scanner/scanner-tests
./build/backend/riscv/riscv-tests --gtest_filter='BookTest.*'   # every book run test on RISC-V
```

To run just one test by name:

```sh
./build/backend/riscv/riscv-tests --gtest_filter='BookTest.Chapter1_Return2'
./build/backend/besm6/besm-tests --gtest_filter='BookTest.Chapter1_Return2'    # same program, BESM-6
```

The RISC-V run tests need clang with RISC-V support, `ld.lld` and `qemu-system-riscv64`;
without them they report *skipped*. A full `BookTest.*` run on RISC-V boots qemu twice per
program (ours and clang's), so expect it to take a couple of minutes. The BESM-6 run tests
need the `b6as`/`b6ld`/`b6sim` tools from the sibling v7besm project on `PATH`. Don't run
two copies of the same test binary at once: they share scratch filenames, and the fixtures
detect the clash and fail the test rather than let the runs clobber each other's files.

## 15. Takeaways

If you remember three things from this article, make them these:

1. **Tests are how you make change safe.** A dense, automated suite turns "I hope I didn't
   break anything" into "the computer just confirmed I didn't." That is what lets a compiler
   grow without rotting — and, here, what let a second backend grow on top of the first.
2. **An error is a classifier — but the classifier can disagree with the book.** The kind of
   mistake usually tells you which phase should catch it (bad token → scanner, bad grammar →
   parser, bad meaning → semantic). Yet because our parser is more permissive and our type
   checker is unified, many programs reclassify across phases — and our no-shadowing rule
   turns some *valid* programs into negatives. The directory is a hypothesis, not a verdict.
3. **The target decides which valid programs can even run.** On RISC-V, whose data model is
   the book's, every shared program runs and agrees with clang. On the BESM-6, 41-bit
   integers, 48-bit unsigneds, NaN-free floats and word addressing mean a *correct* C program
   can legitimately compute a different answer — so that target skips a well-understood slice
   of the suite, each with a one-line reason, and runs its own versions instead. Honest
   skipping beats a doctored green checkmark.

From here, the natural next steps are to browse the committed chapter test files named
throughout this article (`scanner/test/chapter1_tests.cpp`, `backend/common/test/book/chapter13_tests.cpp`,
and their siblings), the two `BookTest` fixtures
([RISC-V](../backend/riscv/test/book_test.h), [BESM-6](../backend/besm6/test/book_test.h)),
and to consult [Riscv_Backend.md](Riscv_Backend.md),
[Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md) and the
[Technical Reference](Technical_Reference.md) for the full phase-by-phase design and the
targets' data models.
