#include "codegen_test.h"

//
// Task #26 framework + rule #27 (redundant reload elimination) + rule #28 (dead
// temp-store elimination).
//
// Rule #27: an `atx reg,off` that stores A to a frame slot leaves the value in A,
// so an immediately following `xta reg,off` reload of the same slot is redundant
// and is deleted.  See backend/besm6/Peephole_Rewrites.md §5.1.
//
// Rule #28: once #27 removes the reload, the `atx` of a single-use '%'-temporary is
// dead — nothing reads the slot before the block ends or it is overwritten — and is
// removed too, leaving the result live only in A.  See backend/besm6/Peephole_Rewrites.md §5.2.
//

// The sum temporary's store and reload are both gone: #27 drops the `7 ,xta,` reload
// and #28 drops the now-dead `7 ,atx,` store, so the sum flows straight from A into
// the global `g`.
TEST_F(CodegenTest, RedundantReloadRemoved)
{
    std::string output = CompileToMadlen("extern int g; void foo(int a, int b) { g = a + b; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,a+x, 1
             ,utc, g
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// A volatile local is reloaded after its store: rule #27 leaves a volatile access as
// it was written.
TEST_F(CodegenTest, VolatileReloadKept)
{
    std::string output = CompileToMadlen("int f(int a) { volatile int x = a; return x; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
          15 ,utm, 1
           6 ,xta,
           7 ,atx,
           7 ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// A label is a basic-block boundary: control can re-enter, so a slot stored just
// before the label must NOT license dropping a reload of it just after.  The
// if/else join stores `c` (slot 7,0) at the end of the else arm, then the join
// label `*1`, then reloads `c` for the return.  Because the label resets the
// tracked state, the `7 ,xta,` reload after `*1:` survives (it is NOT collapsed
// against the preceding `7 ,atx,`, which lies in a different basic block).
TEST_F(CodegenTest, ReloadAcrossLabelKept)
{
    std::string output = CompileToMadlen(
        "int foo(int a, int b) { int c; if (a) c = a; else c = b; return c; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
          15 ,utm, 1
           6 ,xta,
             ,uza, *0
           6 ,xta,
           7 ,atx,
             ,uj, *1
       *0:   ,bss,
           6 ,xta, 1
           7 ,atx,
       *1:   ,bss,
           7 ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Rule #28 in full: `return a + b;` routes the sum through a single-use temporary.
// #27 drops the reload, #28 drops the dead store, and the sum is returned straight
// from A — no `7 ,atx,` temp store survives.  (The temp's frame slot is still counted
// in `15 ,utm, 1`; reclaiming it is task #35.)
TEST_F(CodegenTest, DeadTempStoreRemoved)
{
    std::string output = CompileToMadlen("int foo(int a, int b) { return a + b; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,a+x, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

// Multi-block guard: a temporary that is live across a basic-block boundary must keep
// its store.  The ternary `a ? b : c` writes its result temporary (slot 7,0) at the end
// of each arm and reloads it after the join label `*1`.  Because that temporary is
// referenced in three basic blocks, rule #28 must NOT treat either `7 ,atx,` as dead —
// both stores survive.
TEST_F(CodegenTest, TempLiveAcrossBranchKept)
{
    std::string output = CompileToMadlen("int foo(int a, int b, int c) { return a ? b : c; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
          15 ,utm, 1
           6 ,xta,
             ,uza, *0
           6 ,xta, 1
           7 ,atx,
             ,uj, *1
       *0:   ,bss,
           6 ,xta, 2
           7 ,atx,
       *1:   ,bss,
           7 ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Behavior guard for the multi-block case: dead temp-store elimination must not
// corrupt a value that is live across a branch.  Both ternary results must compute.
TEST_F(CodegenTest, TempAcrossBranchBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        int pick(int a, int b, int c) { return a ? b : c; }
        void program(void) {
            printf("%d %d\n", pick(1, 17, 25), pick(0, 17, 25));
        }
    )");
    EXPECT_EQ("17 25\n", out);
}

// Rule #29 — NTR mode coalescing.  Two double additions in one basic block emit a
// `ntr 7` (restore) immediately chased by a `ntr 0` (re-enter FP mode).  Rule #29(b)
// drops the dead `ntr 7` and #29(a) drops the now-redundant `ntr 0`, so R = 0 is held
// across both `a+x` and restored once at the end: a single `,ntr, 0` … `,ntr, 7`
// bracket.  See backend/besm6/Peephole_Rewrites.md §5.3.
TEST_F(CodegenTest, ConsecutiveFpOpsCoalesceNtr)
{
    std::string output = CompileToMadlen(
        "double f(double a, double b, double c) { double e = a + b; return e + c; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,ntr, 0
           6 ,a+x, 1
           6 ,a+x, 2
             ,ntr, 7
             ,uj, b/ret
             ,end,
)",
              output);
}

// Behavior guard for rule #29: coalescing the NTR brackets must not change the FP
// result.  Two consecutive FP adds, (a + b) + c, with normalization/rounding active
// throughout.  The result is printed as its raw octal bit pattern (the runtime has no
// %f formatting); 8.0 encodes as 4210000000000000.
TEST_F(CodegenTest, FpCoalesceBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        double add3(double a, double b, double c) { double e = a + b; return e + c; }
        void program(void) {
            printf("%o\n", add3(1.5, 2.5, 4.0));
        }
    )");
    EXPECT_EQ("4210000000000000\n", out);
}

// Task #30 — compare → branch fusion.  A relational helper's 0/1 result that feeds a
// JUMP_IF_ZERO is consumed directly by the conditional branch: no boolean temp is stored
// or reloaded.  This is the emergent product of rule #27 (which drops the reload) and rule
// #28 (which drops the now-dead store), made correct by the runtime helpers' logical-ω
// exit contract — every comparison helper leaves ω consistent with its returned A, so the
// `,uza,` tests the result without a reload.  See backend/besm6/Peephole_Rewrites.md §5.4 and the
// "ω mode and the AU mode register R" section of backend/besm6/Besm6_Runtime_Library.md.
//
// The fused shape is `xta / xts / ,call, b/lt / ,uza,` with no `,atx,`/`,xta,` of the
// comparison temporary between the call and the branch.
TEST_F(CodegenTest, CompareBranchFused)
{
    std::string output =
        CompileToMadlen("extern int g; void foo(int a, int b) { if (a < b) g = 1; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,xts, 1
             ,call, b/lt
             ,uza, *1
             ,xta, =1
             ,utc, g
             ,atx,
             ,uj, *2
       *1:   ,bss,
       *2:   ,bss,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Behavior guard for the compare → branch fusion across signedness and FP: each
// comparison drives an if/else and prints a distinguishing digit (the listing path
// upper-cases output, so letters cannot encode a true/false distinction).  The expected
// string 0101110 is, in order: a<b (7<4) 0, a>b 1, a==b 0, a!=b 1, ua<ub (3<9) 1,
// x<y (1.5<2.5) 1, x>=y 0 — confirming every helper's ω feeds the branch correctly.
TEST_F(CodegenTest, CompareBranchFusedBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        void program(void) {
            volatile int a = 7, b = 4;
            volatile unsigned ua = 3u, ub = 9u;
            volatile double x = 1.5, y = 2.5;
            printf("%d", (a < b) ? 1 : 0);
            if (a > b)   printf("1"); else printf("0");
            if (a == b)  printf("1"); else printf("0");
            if (a != b)  printf("1"); else printf("0");
            if (ua < ub) printf("1"); else printf("0");
            if (x < y)   printf("1"); else printf("0");
            if (x >= y)  printf("1"); else printf("0");
            printf("\n");
        }
    )");
    EXPECT_EQ("0101110\n", out);
}

// Behavior must be unchanged with the pass on (the pass is always on): the
// store/reload that rule #27 collapses is on the a+b path of this program.
TEST_F(CodegenTest, ArithmeticBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        void program(void) {
            int a = 17;
            int b = 25;
            printf("%d\n", a + b);
        }
    )");
    EXPECT_EQ("42\n", out);
}

// Rule #31 — branch / label cleanup.  See backend/besm6/Peephole_Rewrites.md §5.5.

// Rule #31(b) on the epilogue: `return x;` emits `,uj, b/ret` from the RETURN
// instruction, then the function epilogue emits a second `,uj, b/ret`.  The second is
// unreachable (it follows an unconditional transfer with no intervening label), so the
// unreachable-tail rule deletes it, leaving a single `,uj, b/ret` before `,end,`.
TEST_F(CodegenTest, DuplicateEpilogueJumpRemoved)
{
    std::string output = CompileToMadlen("int foo(int a) { return a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Rule #31(c) — invert a conditional that only skips an unconditional jump, with a named
// target.  `if (a) goto done;` lowers to `uza *0 / uj *L2 / *0:` (skip the goto when the
// guard is false; the user label `done` is renamed to the unit-unique `%L2`, emitted `*L2`
// in Madlen).  The rule folds it to a single `,u1a, *L2` — take the jump when the guard is
// nonzero — and leaves the now-unreferenced skip label `*0:` in place.
TEST_F(CodegenTest, ConditionalOverJumpInverted)
{
    std::string output =
        CompileToMadlen("int foo(int a) { if (a) goto done; a = 5; done: return a; }");
    EXPECT_EQ(R"(c
      foo:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,u1a, *L2
       *0:   ,bss,
             ,xta, =5
           6 ,atx,
      *L2:   ,bss,
           6 ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Behavior guard for the inversion: the rewritten branch must take the same path.
// a != 0 skips the assignment and returns a; a == 0 falls through, assigns 5, returns 5.
TEST_F(CodegenTest, ConditionalOverJumpBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        int pick(int a) { if (a) goto done; a = 5; done: return a; }
        void program(void) {
            printf("%d %d\n", pick(7), pick(0));
        }
    )");
    EXPECT_EQ("7 5\n", out);
}

// Rule #31(a) — jump to the immediately following label.  The current frontend never
// emits `uj L` directly before `L:` (lowering either inverts the skip or places code
// between), so this is a white-box test: build that shape directly and confirm the pass
// deletes the redundant `uj`, leaving the label.
TEST_F(CodegenTest, JumpToNextLabelRemoved)
{
    Besm_Func *fn    = besm_new_func("foo", BESM_CC_INTERNAL);
    Besm_Block *blk  = besm_new_block();
    Besm_Instr *uj   = besm_new_instr(BESM_BRANCH_UJ);
    uj->name         = xstrdup("L");
    Besm_Instr *lbl  = besm_new_instr(BESM_STMT_LABEL);
    lbl->name        = xstrdup("L");
    Besm_Instr *end  = besm_new_instr(BESM_STMT_END);
    uj->next         = lbl;
    lbl->next        = end;
    blk->body        = uj;
    fn->blocks       = blk;

    besm_peephole(fn, nullptr);

    // The `uj L` is gone; the list is just `L:` then `,end,`.
    ASSERT_NE(nullptr, blk->body);
    EXPECT_EQ(BESM_STMT_LABEL, blk->body->kind);
    ASSERT_NE(nullptr, blk->body->next);
    EXPECT_EQ(BESM_STMT_END, blk->body->next->kind);
    EXPECT_EQ(nullptr, blk->body->next->next);

    besm_free_func(fn);
}

// Rule #31(b) — unreachable tail in its general form: an arbitrary instruction (not just
// a duplicate jump) sitting between an unconditional `uj` and the next label is dead.
// The frontend never produces this (the TAC unreachable-code pass removes dead C
// statements earlier), so it is exercised white-box.  Here `uj other` is followed by a
// stray `xta` then the label `end:`; the `xta` must be deleted.  The jump targets a
// different label, so the `uj` and `end:` both survive (no jump-to-next-label cascade).
TEST_F(CodegenTest, UnreachableTailRemoved)
{
    Besm_Func *fn    = besm_new_func("foo", BESM_CC_INTERNAL);
    Besm_Block *blk  = besm_new_block();
    Besm_Instr *uj   = besm_new_instr(BESM_BRANCH_UJ);
    uj->name         = xstrdup("other");
    Besm_Instr *dead = besm_new_instr(BESM_MEM_XTA); // unreachable: never executed
    dead->reg        = 7;
    Besm_Instr *lbl  = besm_new_instr(BESM_STMT_LABEL);
    lbl->name        = xstrdup("end");
    Besm_Instr *end  = besm_new_instr(BESM_STMT_END);
    uj->next         = dead;
    dead->next       = lbl;
    lbl->next        = end;
    blk->body        = uj;
    fn->blocks       = blk;

    besm_peephole(fn, nullptr);

    // The unreachable `xta` is gone; `uj end` and the label survive.
    ASSERT_NE(nullptr, blk->body);
    EXPECT_EQ(BESM_BRANCH_UJ, blk->body->kind);
    ASSERT_NE(nullptr, blk->body->next);
    EXPECT_EQ(BESM_STMT_LABEL, blk->body->next->kind);
    ASSERT_NE(nullptr, blk->body->next->next);
    EXPECT_EQ(BESM_STMT_END, blk->body->next->next->kind);

    besm_free_func(fn);
}

// C-group atomicity: the instruction following a UTC or WTC reads mem[C], not the frame
// slot its (reg, addr) fields spell out, and deleting it would leave the setter to re-bind
// C to whatever fell into the gap.  Here `7 ,atx, 3` leaves A mirroring slot (7,3), then
// `7 ,wtc, 3` sets C from that slot's pointer word, and `7 ,xta, 3` — textually the same
// slot — actually loads mem[C + r7 + 3].  A rule that matched on (reg, addr) alone would
// delete the `xta` as a redundant reload.  It must survive.
TEST_F(CodegenTest, CConsumerNotDeletedAdjacentToSetter)
{
    Besm_Func *fn   = besm_new_func("foo", BESM_CC_INTERNAL);
    Besm_Block *blk = besm_new_block();

    Besm_Instr *atx = besm_new_instr(BESM_MEM_ATX); // A mirrors slot (7,3)
    atx->reg        = 7;
    atx->addr       = 3;
    Besm_Instr *wtc = besm_new_instr(BESM_MOD_WTC); // C = mem[r7+3]
    wtc->reg        = 7;
    wtc->addr       = 3;
    Besm_Instr *xta = besm_new_instr(BESM_MEM_XTA); // A = mem[C + r7 + 3]
    xta->reg        = 7;
    xta->addr       = 3;

    atx->next  = wtc;
    wtc->next  = xta;
    blk->body  = atx;
    fn->blocks = blk;

    besm_peephole(fn, nullptr);

    ASSERT_NE(nullptr, blk->body);
    EXPECT_EQ(BESM_MEM_ATX, blk->body->kind);
    ASSERT_NE(nullptr, blk->body->next);
    EXPECT_EQ(BESM_MOD_WTC, blk->body->next->kind);
    ASSERT_NE(nullptr, blk->body->next->next);
    EXPECT_EQ(BESM_MEM_XTA, blk->body->next->next->kind);
    EXPECT_EQ(nullptr, blk->body->next->next->next);

    besm_free_func(fn);
}

// Rule #27 through a C group: `g.x = 7; return g.x;` stores to the global via
// `,utc, g / ,atx,` and would reload it via `,utc, g / ,xta,`.  A location is a global
// plus a word offset, not just a frame slot, so the reload group — setter *and* consumer,
// deleted together — is recognised as redundant and the 7 flows straight from A into the
// return.  (TAC copy propagation cannot do this: `copy_from_offset` is not a copy.)
TEST_F(CodegenTest, RedundantGlobalReloadRemoved)
{
    std::string output = CompileToMadlen(
        "struct Foo { int x; int y; }; struct Foo g; int f(void) { g.x = 7; return g.x; }");
    EXPECT_EQ(R"(c
        g:   ,name,
             ,bss, 2
             ,end,
c
        f:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta, =7
             ,utc, g
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// The complement: by the time `g.x` is reloaded, A holds the 1 that went out through `p`,
// not `g.x`.  Both instructions in between say so — the `,xta, =1` names no location, and
// the `6 ,wtc, / ,atx,` dereference names none either — so the tracked location is unknown
// and the reload group survives.  Were it deleted, `f` would return 1 whenever `p` points
// anywhere but `g.x`.
TEST_F(CodegenTest, GlobalReloadAcrossAliasingStoreKept)
{
    std::string output = CompileToMadlen("struct Foo { int x; int y; }; struct Foo g; "
                                         "int f(int *p) { g.x = 7; *p = 1; return g.x; }");
    EXPECT_EQ(R"(c
        g:   ,name,
             ,bss, 2
             ,end,
c
        f:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
             ,xta, =7
             ,utc, g
             ,atx,
             ,xta, =1
           6 ,wtc,
             ,atx,
             ,utc, g
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Behavior guard for the global reload rewrite.  `getx` must see its own store (7).
// `viaptr` reloads `g.x` after storing 1 through `p`, so the reload must survive and read
// memory: 3 when `p` points elsewhere, 1 when it aliases `g.x`.  Both calls are needed —
// deleting the reload leaves A holding the 1 that went through `p`, which is accidentally
// right in the aliasing call and wrong (1 instead of 3) in the non-aliasing one.
TEST_F(CodegenTest, GlobalReloadBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        struct Foo { int x; int y; };
        struct Foo g;
        int q;
        int getx(void) { g.x = 7; return g.x; }
        int viaptr(int *p) { g.x = 3; *p = 1; return g.x; }
        void program(void) {
            printf("%d %d %d\n", getx(), viaptr(&q), viaptr(&g.x));
        }
    )");
    EXPECT_EQ("7 3 1\n", out);
}

// Rule #27 through a dereference.  `*p = x; return *p;` stores via `6 ,wtc, / ,atx,` and
// reloads via `6 ,wtc, / ,xta,` — the same location, since nothing writes `p` in between.
// The reload group goes and the value flows out of A.  TAC copy propagation cannot reach
// this either: a Store is not a copy, so the Load survives to instruction selection.  This
// is the shape the C-group model was built for.
TEST_F(CodegenTest, RedundantDerefReloadRemoved)
{
    std::string output = CompileToMadlen("int f(int *p, int x) { *p = x; return *p; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta, 1
           6 ,wtc,
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// The same rewrite through a global pointer.  Its word is not in the frame, so the
// dereference is `,wtc, gp` (C = gp, WTC's 15-bit address field reaches any global) + the
// access.  Both nodes of the reload are spliced out together.
TEST_F(CodegenTest, DerefReloadThroughGlobalPointerRemoved)
{
    std::string output = CompileToMadlen("int *gp; int f(int x) { *gp = x; return *gp; }");
    EXPECT_EQ(R"(c
       gp:   ,name,
             ,bss, 1
             ,end,
c
        f:   ,name,
    b/ret:   ,subp,
       gp:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,wtc, gp
             ,atx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// A store through a *different* pointer leaves A mirroring `*q`, not `*p`, so the reload of
// `*p` must survive: `q` may point anywhere.  The two locations differ by the frame slot
// their pointer lives in (`6 ,wtc,` vs `6 ,wtc, 1`), which is what `loc_eq` compares.
TEST_F(CodegenTest, DerefReloadAcrossOtherPointerStoreKept)
{
    std::string output =
        CompileToMadlen("int f(int *p, int *q, int x, int y) { *p = x; *q = y; return *p; }");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta, 2
           6 ,wtc,
             ,atx,
           6 ,xta, 3
           6 ,wtc, 1
             ,atx,
           6 ,wtc,
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Behavior guard for the dereference rewrite, three ways.  `self` must return its own store
// (5).  `viaq` stores 2 through `q` before reloading `*p`; deleting that reload would leave
// A holding 2 and return it instead of 1.  `viacall` has `bump` overwrite `*p` behind its
// back, so its reload must survive the call boundary and read 99, not the stored 7.
TEST_F(CodegenTest, DerefReloadBehaviorUnchanged)
{
    std::string out = CompileAndRun(R"(
        #include <stdio.h>
        int ga;
        int gb;
        int *gp;
        int self(int *p, int x) { *p = x; return *p; }
        int viaq(int *p, int *q, int x, int y) { *p = x; *q = y; return *p; }
        int bump(void) { *gp = 99; return 0; }
        int viacall(int *p, int x) { *p = x; bump(); return *p; }
        void program(void) {
            gp = &ga;
            printf("%d %d %d\n", self(&ga, 5), viaq(&ga, &gb, 1, 2), viacall(&ga, 7));
        }
    )");
    EXPECT_EQ("5 1 99\n", out);
}

//
// Rule #32 — I/O address folding.  See backend/besm6/Peephole_Rewrites.md §5.10.
//
// Instruction selection always delivers a non-constant `ext`/`mod`/extracode address through
// the stack (`xts` pushes it while loading the accumulator operand, a stack-mode `wtc` pops
// it into C).  #32(b) collapses that to a single `wtc` when the address was merely loaded
// out of memory, and #32(a) moves a constant addend into the instruction's own address
// field.  Together they turn six nodes into three.
//

// Both rules on the same call: `x + 1` on the header's `unsigned` parameter lowers to
// `xta x` + `xts =1` + `call b/uadd`, and the folded result reads the address straight out
// of the frame slot with the displacement in the `ext` itself.  Nothing is left of the
// addition, and no index register is touched.
TEST_F(CodegenTest, IoAddressFoldedToWtc)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned poke(unsigned x, unsigned w) { return __besm6_ext(x + 1, w); }
    )");
    EXPECT_EQ(R"(c
     poke:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta, 1
           6 ,wtc,
             ,ext, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

// The displacement bound.  010000 does not fit the 12-bit Format-1 address field, so #32(a)
// must decline — and with the addition still in the way, #32(b) has no memory-resident base
// to anchor on either.  The whole stack form survives, which is what keeps the address
// correct rather than truncated into the field.
TEST_F(CodegenTest, IoAddressDisplacementTooLargeKept)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned poke(unsigned x, unsigned w) { return __besm6_ext(x + 010000, w); }
    )");
    EXPECT_EQ(R"(c
     poke:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,xts, =10000
             ,call, b/uadd
           6 ,xts, 1
          15 ,wtc,
             ,ext,
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// Rule #33 — ω fixup before a conditional branch.  See backend/besm6/Peephole_Rewrites.md §5.11.
//
// `uza`/`u1a` test ω, and ω means "A = 0?" only under the logical group.  Instruction
// selection loads the condition with an `xta` (logical) right before the branch, but rules
// #27 and #28 delete that store/reload pair whenever A already holds the value — exposing
// the group its producer left.  `x - y` leaves *additive* ω, under which the `uza` would
// branch on A ≥ 0: a sign test, not a truth test.  The `,aex,` (A ^= memory word 0: A
// unchanged, ω logical) is what makes the branch mean what C wrote.
//
TEST_F(CodegenTest, OmegaFixupAfterSubtract)
{
    std::string output = CompileToMadlen("int diff(int x, int y) { if (x - y) return 1; return 0; }");
    EXPECT_EQ(R"(c
     diff:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,a-x, 1
             ,aex,
             ,uza, *1
             ,xta, =1
             ,uj, b/ret
       *1:   ,bss,
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Unary minus lowers to `x-a`, which is additive too, so the same fixup applies.
TEST_F(CodegenTest, OmegaFixupAfterUnaryNegate)
{
    std::string output = CompileToMadlen("int neg(int x) { int b = -x; if (b) return 1; return 0; }");
    EXPECT_EQ(R"(c
      neg:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,x-a,
             ,aex,
             ,uza, *1
             ,xta, =1
             ,uj, b/ret
       *1:   ,bss,
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}

// The loop shape reloads the condition each iteration, so the fixup rides inside the loop
// body — once per test, not once per function.
TEST_F(CodegenTest, OmegaFixupInLoopCondition)
{
    std::string output =
        CompileToMadlen("extern int g; void loop(int x, int y) { while (x - y) g = 1; }");
    EXPECT_EQ(R"(c
     loop:   ,name,
    b/ret:   ,subp,
        g:   ,subp,
             ,its, 13
             ,call, b/save
      *L1:   ,bss,
           6 ,xta,
           6 ,a-x, 1
             ,aex,
             ,uza, *L0
             ,xta, =1
             ,utc, g
             ,atx,
             ,uj, *L1
      *L0:   ,bss,
             ,uj, b/ret
             ,end,
)",
              output);
}

// The negative case, and the reason the fixup is tracked rather than emitted blindly: a
// plain `xta` of the condition already leaves logical ω, so nothing is inserted.  The
// compare → branch fusion above (CompareBranchFused) is the other one — a relational helper
// returns in logical ω by contract — and IntrinsicExtBranchMadlen is the third, a
// read-address `ext`.
TEST_F(CodegenTest, OmegaFixupNotEmittedAfterLoad)
{
    std::string output = CompileToMadlen("int truth(int x) { if (x) return 1; return 0; }");
    EXPECT_EQ(R"(c
    truth:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,uza, *0
             ,xta, =1
             ,uj, b/ret
       *0:   ,bss,
             ,xta,
             ,uj, b/ret
             ,end,
)",
              output);
}
