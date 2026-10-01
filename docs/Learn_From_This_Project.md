# What This Project Is Really About

*A personal teacher's breakdown of everything we built together.*

---

## Step 1: The Approach and Why

### The Starting Point: What Problem Are We Actually Solving?

You set out to build a **C compiler that targets the BESM-6** — a 1965 Soviet mainframe. But that sentence
buries the lead. The real problem is: *how do you take source code written in a high-level language and
turn it into a sequence of instructions for a machine that has never heard of variables, functions, or
types?*

The answer, which is the core insight of every compiler ever written, is: **you don't do it in one step.
You do it in layers, and each layer's only job is to get slightly closer to the machine.**

The approach taken here follows the classical layered pipeline:

```
Human-readable text (C source)
      ↓
Tokens (scanner)
      ↓
Tree structure (AST — parser)
      ↓
Validated, typed tree (semantic analysis)
      ↓
Flat, explicit instructions (TAC — translator)
      ↓
Machine instructions (BESM-6 assembly)
```

**Why this layering?** Because each layer is a fundamentally different kind of transformation:
- Tokens → Tree: recognizing *structure* (grammar)
- Tree → Typed tree: recognizing *meaning* (semantics)
- Tree → TAC: eliminating *complexity* (expressions become single operations)
- TAC → Assembly: eliminating *abstraction* (variables become memory addresses)

Mixing these together would be like trying to bake bread by doing all the steps simultaneously — knead,
bake, and slice at the same time. Instead, you stage it. Each stage has clear inputs and outputs, and
you can test each one independently.

### Why TAC as the Pivotal IR?

The single most important architectural decision in this project is the choice of **Three-Address Code
(TAC)** as the intermediate representation (IR) — the hub that connects the frontend to all backends.

TAC is beautifully simple: every instruction has at most two source operands and one destination.
That's it. No nested expressions. No complex addressing modes. Just:

```
%1 = b * 2
%2 = a + %1
```

This format is *close enough to hardware* that backends can translate it mechanically, but *far enough
from hardware* that the frontend doesn't need to know anything about registers or instruction sets.
It's the Goldilocks IR — not too high-level, not too low-level.

The practical payoff: **the entire frontend (scanner, parser, semantic analysis, translator) was
completed once and works for all backends** — BESM-6, x86_64, and any future targets. You wrote
it once and it's done.

---

## Step 2: Roads Not Taken — What We Considered and Rejected

### Alternative 1: Direct AST-to-Assembly Translation

The simplest approach would be to walk the AST and directly emit BESM-6 instructions without an IR.
Many toy compilers work this way.

**Why it was rejected:** It creates an explosion of backend complexity. Every time you add a new
construct to the language, you need to implement it in every backend. With TAC as the hub, you add
the construct once in the translator, and all backends get it for free. More importantly, it makes
testing much harder — you can't test the frontend without a functioning backend.

The BESM-6 has unusual characteristics (single accumulator, no register-register operations, 48-bit
words). If the translator were tightly coupled to BESM-6 specifics, the whole codebase would be
poisoned by them.

### Alternative 2: SSA Form as the IR

Many modern compilers (LLVM, GCC's GIMPLE) use **Static Single Assignment (SSA)** form, where each
variable is assigned exactly once. SSA enables powerful optimizations.

**Why it was rejected:** SSA is significantly more complex to construct and reason about. It requires
a dominator-tree analysis and phi-node insertion pass. For a compiler whose goal is correctness and
clarity — not optimization — that complexity has no payoff. TAC is sufficient to represent all C
semantics faithfully, and it maps naturally to hardware without SSA machinery.

This is a deliberate **pedagogical choice**: build the thing that works correctly and is
understandable, not the thing that's theoretically optimal.

### Alternative 3: Using an Existing IR (LLVM IR, etc.)

You could have used LLVM as the backend.

**Why rejected:** LLVM is enormous, complex, and targets modern architectures. It has no BESM-6
backend and almost certainly never will. More importantly, the goal of this project is to *understand*
how compilers work, not to wrap an existing one. Building your own IR is how you actually learn.

### Alternative 4: Code Generation from a Grammar Tool

lex/yacc (or flex/bison) are traditional tools for building scanners and parsers.

**Why rejected:** The scanner and parser were written by hand (recursive descent). This means the
entire parsing logic is visible in ordinary C code — no domain-specific language, no magic. When
something goes wrong, you can read the source. The `.l`/`.y` files in `grammar/` exist as
**documentation** (showing the formal grammar), not as code generators.

### Alternative 5: Allowing Variable Shadowing

C allows inner blocks to redeclare names from outer scopes. Many compilers implement this.

**Why rejected:** This was a deliberate, permanent design decision. Allowing shadowing requires
every scope-aware lookup to walk a scope chain. Without shadowing, the symbol tables (`symtab`,
`structtab`, `typetab`) are simpler — a flat map that just rejects duplicates with `fatal_error`.
The compiler is targeting Unix v7, where shadowing is rare and its absence doesn't matter much.
The tradeoff: stricter rules for the developer, much simpler implementation.

---

## Step 3: How the Parts Connect

Think of the project as a relay race. Each runner (phase) receives a baton, does their leg, and
hands it to the next runner. The baton changes form at each handoff.

```
[Source .c file]
      |
      | Characters
      ↓
[Scanner] — scanner/
      |
      | Token stream
      ↓
[Parser] — parser/main.c
      |
      | Binary AST stream (wio format)
      ↓
[Semantic Analysis] — semantic/
      |
      | Annotated AST (types resolved, scope checked)
      ↓
[Translator] — translator/
      |
      | TAC instructions (flat, typed, explicit)
      ↓
[BESM-6 Codegen] — backend/besm6/
      |
      | Madlen assembly (.mad)
      ↓
[Madlen Assembler] — external tool
```

**Why binary format between stages?** AST and TAC are serialized as binary streams using `wio`
(word-oriented I/O). This means you can pipe stages together on the command line, inspect
intermediate representations, and test each stage in isolation. The YAML export exists purely
for humans — it's for debugging and education. The binary format is for machines.

**Why separate tools (`parse`, `lower`, `genbesm`) rather than one monolithic compiler?**
Because it lets you test each phase independently. `./build/lower --yaml /tmp/x.ast` shows
you exactly what the TAC for a given C file looks like, without involving the backend at all.
This is invaluable for debugging.

### The BESM-6 Backend: Where It All Comes Together

The backend is where the rubber meets the road. TAC says "add these two integers" — the
backend has to figure out how to do that on a machine with:
- One accumulator register (A)
- No register-register operations
- 48-bit words
- Integer values encoded in a floating-point format with a specific exponent

The backend (`codegen.c`) implements a disciplined **load-operate-store** pattern:
1. Load source operand into accumulator A via `XTA`
2. Perform operation (ADD, SUB, etc.)
3. Store result via `ATX`

Every TAC instruction maps to a short sequence of BESM-6 instructions. The frame allocator
assigns each variable a slot (indexed from r6 for parameters, r7 for locals), and the
codegen looks up each variable's slot to emit the right address.

---

## Step 4: Tools, Methods, and Frameworks

### GoogleTest (C++17 for tests)

The implementation is in C11, but all tests are C++17 using GoogleTest. Why?

**Because testing C code from C++ is a well-established pattern.** GoogleTest gives you
named test cases, descriptive failure messages, test filters (`--gtest_filter`), and a
mature ecosystem. Writing tests in plain C would mean rolling your own assertion library.

The cost: you need a C++ compiler alongside your C compiler, and there's a language boundary
to cross in tests. The benefit: vastly better test infrastructure.

### ASDL (Abstract Syntax Description Language)

The `.asdl` files (`ast/ast.asdl`, `tac/tacky.asdl`, `backend/besm6/besm6.asdl`) are
not used for code generation. They're executable documentation.

ASDL is the language that Python uses to define its AST. It reads like algebra:

```asdl
expr = Binary(binary_op op, expr left, expr right)
     | Unary(unary_op op, expr operand)
     | Var(identifier name)
     | Const(const value)
```

By keeping ASDL specs and hand-written C headers in sync, you get the best of both worlds:
a human-readable specification AND full control over memory layout and field naming.

### xalloc — The Memory Strategy

Rather than carefully freeing every allocation, the compiler uses **arena-style bulk
deallocation**: allocate through `xalloc`, and call `xfree_all()` at shutdown. This works
because compilers are batch programs — they run, produce output, and exit. There's no need
for precise per-object lifetime management.

In debug builds, `xalloc_report()` shows exactly how much memory was allocated and what
wasn't freed. This makes leak detection effortless.

### wio — Word-Oriented I/O

The binary IR format uses `size_t`-wide "words" rather than bytes. Why? Because the
compiler might run on a 32-bit machine but emit code for a 64-bit target (or vice versa).
Using word-sized units makes the serialization format portable across word sizes.
Each TAC node is tagged with a 4-letter readable ASCII tag (`cnst`, `insr`, `tval`) that
makes the binary stream hand-readable in a hex editor.

---

## Step 5: Tradeoffs — What Was Prioritized and What Was Sacrificed

| Decision | What You Gained | What You Sacrificed |
|---|---|---|
| TAC as IR | Clean frontend/backend separation; testable in isolation | Extra phase, more code |
| No shadowing | Simpler symbol tables | C compatibility (some programs rejected) |
| ASDL as documentation only | Full control over C layout | No auto-generated code |
| Bulk deallocation (xalloc) | No recursive free functions | Memory not freed until shutdown |
| Hand-written parser | Debuggable, readable, no magic | More lines of code than yacc |
| Binary IR + YAML export | Both efficiency and debuggability | Two code paths to maintain |
| Complete frontend before backend | Solid foundation; test in isolation | Longer time to see assembly output |
| INT-format integers on BESM-6 | Arithmetic works; normalization suppressed | Non-obvious encoding for integers |
| TAC without optimization | Simpler translator | Generated code is naive (many temporaries) |
| b/save + b/ret runtime helpers | Short, simple prologues | Runtime dependency; extra call overhead |

The defining tradeoff of the whole project: **correctness and clarity over performance and
completeness.** This is the right tradeoff for an educational compiler targeting a historical
machine. You're not competing with GCC.

---

## Step 6: Mistakes, Dead Ends, and Wrong Turns

### The `Besm_MReg` Abstraction That Didn't Survive

Early in the BESM-6 backend, there was a `Besm_MReg` type — a distinct abstraction for
index registers (the BESM-6 equivalent of base/index registers). It was removed (commit
`f4aadcd`). Why? It added a layer of indirection without real benefit. Index registers in
BESM-6 are just small integers (0–15). A dedicated type for them created boilerplate
without adding safety or clarity.

**Lesson:** Don't prematurely abstract. If a concept is just an integer, name it clearly
and use an integer. Add wrapper types when you have evidence they prevent bugs, not before.

### The Nested Union Model in Besm_Instr

The BESM-6 instruction struct originally used a nested union model (kind + per-kind data
in separate union arms). This was flattened (commit `0d439eb`) to a single struct with
`(kind, reg, addr, name)`. Most instruction fields use only a subset of these, but having
a flat structure is simpler to construct and read.

**Lesson:** Discriminated unions (tagged unions) are powerful but verbose in C. When most
variants share the same fields, a flat struct with "not applicable" fields is often cleaner.
Only introduce a union when field reuse would genuinely confuse the reader.

### GetAddress Codegen — Write Correct First, Optimize Second

The initial GetAddress implementation always emitted a full sequence (`UTC` → `VTM` → `ITA`).
This was later optimized (commit `2e7cccc`) to emit a direct `ITA reg` when the offset is
zero. The optimization came from understanding the target machine better — and having a
working reference implementation to test against.

**Lesson:** Write it correctly first, optimize second. The correct slow version is valuable:
it gives you something to test against when you add the optimization.

### Frame Allocator Before Instruction Selection

The frame allocator was built before instruction selection (commit `86cb498`). This is the
right order — you need to know where each variable lives before you can emit instructions
that reference it. Getting the ordering wrong would have required a painful refactor.

**Lesson:** Understand your data dependencies before coding. What does each phase need to
know, and who provides that information?

---

## Step 7: Pitfalls to Watch Out For

### 1. The "One More Feature" Trap in Parsers

Recursive descent parsers grow organically. Each new C construct seems like just one more case.
Before you know it, the expression parser is 400 lines with nested special-casing. The discipline
here: **finish the grammar specification (ASDL) before coding**, then implement exactly what the
spec says.

### 2. Scope Leaks in Symbol Tables

If you forget to call `scope_decrement()` when exiting a block, inner-scope symbols stay in
the table and pollute outer scopes. The fix: `scope_decrement()` is called in exactly one
place, triggered by block-exit AST nodes. Never call it ad hoc.

### 3. INT-Format vs. Float on BESM-6

The BESM-6 uses the same 48-bit word for both integers and floating-point, distinguished by
the exponent field. The integer encoding uses exponent = 104 (octal 0150). If you forget this
and emit a raw integer, the arithmetic unit will treat it as a floating-point number with a
tiny exponent — and the result will be garbage. The calling convention (b/save) sets R=7,
which suppresses normalization, but only for arithmetic. Load/store is your responsibility.

### 4. NTR Must Precede, Not Follow, Arguments

The BESM-6 calling convention requires that `NTR` (normalize transfer to R) comes *before*
arguments are pushed, not after. Getting this order wrong produces a subtle bug: the register
is set based on what was in the accumulator *after* argument setup, not before. This is the
kind of bug that only surfaces under specific calling sequences.

### 5. Don't Share TAC Contexts Across Functions

Each `TacCtx` (translation context) has its own `temp_id` counter. If you accidentally reuse
a context across functions, temporaries from one function will collide with those of another.
The design: create a fresh `TacCtx` per function — it's cheap.

### 6. The `(void)` Parameter Sentinel

The parser represents `f(void)` as a single `Param` node with `TYPE_VOID` and `NULL` name.
This is *not* the same as `f()` (unspecified parameters). If you forget to strip this sentinel
before iterating params, you'll add a void parameter to the symbol table or emit a phantom
argument. The typecheck pass strips it; don't process params before typechecking.

### 7. ASDL Drift

The ASDL specs and C headers must be manually kept in sync. Drift is silent — there's no
compile-time check. Establish a discipline: whenever you add a node kind to a `.asdl` file,
immediately update the corresponding `.h` file. One-step changes prevent one-off divergence.

---

## Step 8: What an Expert Would Notice

### The Binary IR Design Is Unusually Thoughtful

Most toy compilers just pass AST pointers in memory or dump to text and re-parse. This project
uses a proper binary format (wio) with a magic number (`TAC3`), size_t-wide words, and
readable 4-letter ASCII tags. An expert would recognize this as production compiler technique —
it allows offline debugging, binary diffs of IR, and piping between tools. It's the kind of
thing you find in LLVM's bitcode format, just smaller and simpler.

### The Single-Pass Semantic Analysis Is Bold

Most production compilers do multi-pass analysis (once for name resolution, once for type
checking, etc.). Single-pass analysis requires that declarations are visible before use,
which C mostly guarantees at file scope. The expert question: "What about forward
declarations?" The answer: `typecheck_file_scope_var_decl` detects `TYPE_FUNCTION` and
registers it as a function prototype. The design handles the hard cases.

### The BESM-6 Backend Shows Deep ISA Understanding

The b/save/b/ret approach (delegating register save/restore to runtime helpers) is a
deliberate calling-convention choice that minimizes prologue code. An expert would ask:
"What if you don't have a runtime library?" The answer: you'd need to inline the save/restore
logic. The delegation trades code size for a runtime dependency — a tradeoff that makes
sense when you control the runtime environment (Dubna monitor).

### The Lack of Register Allocation Is Not an Oversight

The BESM-6 has almost no general-purpose registers to allocate. All values live in the
stack frame. This actually *simplifies* the backend considerably — there's no spilling
problem because everything is already on the stack. An expert would note this as an
example of how a constrained ISA can sometimes make compiler design easier.

### The Testing Philosophy Is Correct

Tests live alongside the code they test, not in a separate `tests/` directory. Tests are
fine-grained (individual expression types, specific semantic rules). The pipeline tests
(`semantic/pipeline_tests.cpp`) test end-to-end C programs through the frontend. This
is the right test pyramid for a compiler: many unit tests for individual transformations,
some integration tests for full programs.

---

## Step 9: Lessons That Apply Everywhere

### Lesson 1: Stage Your Transformations

The most powerful pattern in this compiler — and in software generally — is the staged
pipeline. Each stage takes a well-defined input and produces a well-defined output. Applied
to data processing, API design, or build systems: define your intermediate forms explicitly,
and connect them with transformations. Don't write one giant function that does everything.

### Lesson 2: The IR Is the Product

The real work of a compiler isn't the parser or the code generator — it's the IR design.
Get TAC right, and both the frontend (which generates TAC) and the backend (which consumes
TAC) become tractable. Get it wrong, and you'll fight the IR at every layer. In data
pipelines, this is the schema. In APIs, this is the contract. The lesson: spend serious
time on your intermediate representation before writing code around it.

### Lesson 3: Test the Seams

The hardest bugs in layered systems live at the boundaries between layers: the AST-to-TAC
handoff, the TAC-to-assembly handoff. This project tests both directions: AST can be
serialized and deserialized (round-trip tested), and TAC YAML can be inspected. Test your
data at the boundaries, not just within each layer.

### Lesson 4: Documentation as Executable Spec

The ASDL files are not just comments — they're a formal specification of what the data
structures must be. Similarly, [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md)
is a precise contract. Having a written spec before implementation forces clarity about
what you're building. Applied to any API or schema: write the spec first, implement second.

### Lesson 5: Simple Memory Management, Early

xalloc's bulk-deallocation strategy is laughably simple — but it works, it's safe, and
it eliminates an entire class of bugs (use-after-free, double-free, leak). In batch
programs with bounded lifetime, this is almost always the right choice. Don't reach for
reference counting or garbage collection until you have evidence that bulk allocation
is insufficient.

### Lesson 6: The Design That Constrains Is a Feature

No shadowing. No implicit type promotions. No undefined behavior in the IR. Every constraint
was a deliberate choice that made the rest of the system simpler. Applied to any project:
ask "what can I forbid?" before asking "what should I support?" The best APIs are
the ones that make the wrong thing impossible, not just the right thing easy.

### Lesson 7: Know Your Target Machine

You can't write a good BESM-6 backend without understanding BESM-6. The INT-format encoding,
the single accumulator, the C register, the index register calling convention — these are
BESM-6-specific facts that the backend must model correctly. Applied generally: you can't
abstract away a target completely. At some point, you have to know what you're compiling to.
Don't confuse a beautiful abstraction (TAC) with an excuse to not understand the metal.

### Lesson 8: Ship Phases, Not Features

The project shipped the frontend completely before touching the backend. Tests pass, YAML
output works, the IR is stable. Then the backend. This is not "do everything in parallel"
— it's "complete each phase before starting the next." The payoff: when a BESM-6 test fails,
you know the IR is correct. The bug is in the backend, not the frontend.

Applied to any project: find your natural phases and don't start phase N+1 until phase N
is solid. Incomplete phases compounding is how projects collapse.

---

## Closing: What This Project Really Is

Strip away the BESM-6, the C grammar, the Madlen assembler. What is this project, really?

It's a demonstration that **hard problems become tractable when you decompose them
systematically.** A C compiler sounds impossibly complex. But "turn text into a token
stream" is a solved problem. "Parse tokens into a tree" is a solved problem. "Walk a tree
and flatten it to three-address form" is a solved problem. "Map abstract operations to
machine instructions" is a solved problem. None of those steps is magic. Each one is a
well-defined transformation between well-defined data structures.

The BESM-6 is the MacGuffin — the thing that makes the project unusual and interesting.
But the real craft is in the layering, the IR design, the test discipline, and the
willingness to say "no, we don't support that" in order to keep the system understandable.

That discipline is what you take to the next project.

---

*Written to explain every decision, not just describe what happened.*
