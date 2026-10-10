//
// The <besm6.h> compiler intrinsics (backend/besm6/Besm6_Intrinsics.md), Tier 2: the five
// bit-manipulation instructions that have no C equivalent — gather, scatter, population
// count, highest set bit, and the machine's own end-around-carry add; Tier 1: the halt, and
// the two supervisor instructions `ext`/`mod` that are the machine's only I/O.
//
// Each is a call in the IR and an inline instruction in the machine code: the golden tests
// below pin that no `,call,` (and no `,subp,`) survives — which matters more than it looks,
// because all nine intrinsic names truncate to the same 8-character Madlen symbol, so one
// left uninterecepted would silently alias another rather than fail to link.  The run tests
// pin the values the hardware actually produces, on all three paths.
//
#include "codegen_test.h"

//
// Instruction selection — Madlen.
//
// A plain population count is __besm6_acx(a, 0), and a zero operand needs no literal at all:
// the `,acx,` is left with an empty address field, so EA = 0 and it reads memory word 0,
// which always reads as zero.
//
TEST_F(CodegenTest, IntrinsicPopcountMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned pcnt(unsigned a) { return __besm6_acx(a, 0); }
    )");
    EXPECT_EQ(R"(c
     pcnt:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,acx,
             ,uj, b/ret
             ,end,
)",
              output);
}

// Gather and scatter: one instruction each, the mask as the memory operand.
TEST_F(CodegenTest, IntrinsicGatherScatterMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned gather(unsigned a, unsigned m) { return __besm6_apx(a, m); }
        unsigned scatter(unsigned a, unsigned m) { return __besm6_aux(a, m); }
    )");
    EXPECT_EQ(R"(c
   gather:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,apx, 1
             ,uj, b/ret
             ,end,
c
  scatter:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,aux, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// Instruction selection — Unix (b6as).
//
TEST_F(CodegenTest, IntrinsicHighbitUnix)
{
    std::string output = CompileToUnix(R"(
        #include <besm6.h>
        unsigned highbit(unsigned a) { return __besm6_anx(a, 0); }
    )");
    EXPECT_EQ(R"(    .text
    .globl highbit
highbit:
    its 13
 13 vjm b$save
  6 xta
    anx
    uj b$ret
)",
              output);
}

// ARX is the one that leaves multiplicative ω, but nothing here consumes ω — the result is
// returned, not branched on — so no correction is emitted.  The peephole (rule #33) adds one
// only where a conditional branch would otherwise inherit the wrong group; see
// IntrinsicCyclicAddBranchOmega.
TEST_F(CodegenTest, IntrinsicCyclicAddUnix)
{
    std::string output = CompileToUnix(R"(
        #include <besm6.h>
        unsigned cyc(unsigned a, unsigned b) { return __besm6_arx(a, b); }
    )");
    EXPECT_EQ(R"(    .text
    .globl cyc
cyc:
    its 13
 13 vjm b$save
  6 xta
  6 arx 1
    uj b$ret
)",
              output);
}

//
// Instruction selection — Bemsh.  Same two instructions under their Cyrillic mnemonics
// (чед = acx, слц = arx), and again no `внешн` for the intrinsic itself.
//
TEST_F(CodegenTest, IntrinsicPopcountBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        unsigned pcnt(unsigned a) { return __besm6_acx(a, 0); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
pcnt   старт 1
_save  внешн ._save
_ret   внешн ._ret
       счим 13
       пв _save(13)
       сч (6)
       чед
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

TEST_F(CodegenTest, IntrinsicCyclicAddBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        unsigned cyc(unsigned a, unsigned b) { return __besm6_arx(a, b); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
cyc    старт 1
_save  внешн ._save
_ret   внешн ._ret
       счим 13
       пв _save(13)
       сч (6)
       слц 1(6)
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

//
// The ω correction, pinned at the instruction level.
//
// Peephole rules #27 and #28 drop the store/reload of the boolean, so the branch consumes
// the accumulator the ARX left — `,arx,` then `,uza,` with nothing in between.  Under
// multiplicative ω that `uza` would test abs(A) < 0.5 (i.e. bit 48) instead of A ≠ 0.  The
// `,aex,` rule #33 lands between them is what makes the branch test the value.
//
TEST_F(CodegenTest, IntrinsicCyclicAddBranchOmega)
{
    std::string output = CompileToMadlen(R"(
        #include <stdio.h>
        #include <besm6.h>
        void f(unsigned a, unsigned b)
        {
            if (__besm6_arx(a, b))
                puts("NONZERO");
            else
                puts("ZERO");
        }
    )");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,arx, 1
             ,aex,
             ,uza, *1
          14 ,vtm, *str0
             ,ita, 14
             ,aox, =:64
          14 ,vtm, -1
             ,call, puts
             ,uj, *2
       *1:   ,bss,
          14 ,vtm, *str1
             ,ita, 14
             ,aox, =:64
          14 ,vtm, -1
             ,call, puts
       *2:   ,bss,
             ,uj, b/ret
    *str1:   ,log, 2644252223600000
    *str0:   ,log, 2344751626442522
             ,log, 2360000000000000
             ,end,
)",
              output);
}

//
// Execution — Madlen / dubna.  (The KOI7 output device folds letters to upper case, so the
// printed text and the expectations are UPPERCASE.)
//
// popcount(0377) = 8; popcount(0) = 0.  anx numbers from the MSB — bit 48 is position 1 and
// bit 1 is position 48 — so highbit(1) = 48 and highbit(2^47) = 1.  Zero has no highest set
// bit and there is no distinguished "use of undeclared identifier" value: the result is just x,
// here 0.
//
TEST_F(CodegenTest, IntrinsicPopcountRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <besm6.h>
        unsigned pcnt(unsigned a) { return __besm6_acx(a, 0); }
        unsigned hbit(unsigned a) { return __besm6_anx(a, 0); }
        void program()
        {
            printf("%d %d\n", pcnt(0377), pcnt(0));
            printf("%d %d %d\n", hbit(1), hbit(04000000000000000u), hbit(0));
        }
    )");
    EXPECT_EQ(
        "8 0\n"
        "48 1 0\n",
        result);
}

//
// apx gathers the bits of A selected by the mask, aligned to the MSB; aux scatters them
// back into the mask's positions.  They are exact inverses, so a round trip through one
// mask reproduces exactly the masked bits of the original — and nothing else.
//
TEST_F(CodegenTest, IntrinsicGatherScatterRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <besm6.h>
        void program()
        {
            unsigned a = 0525252525252525u;
            unsigned m = 0770077007700770u;
            unsigned r = __besm6_aux(__besm6_apx(a, m), m);
            printf("%o\n", r);
            if (r == (a & m))
                puts("SAME");
            else
                puts("DIFFERENT");
        }
    )");
    EXPECT_EQ(
        "520052005200520\n"
        "SAME\n",
        result);
}

//
// arx is the machine's own integer add: a carry out of bit 48 comes back into bit 1.  This
// is the one place it differs from C's `+` on unsigned, which would wrap to 0.
//
TEST_F(CodegenTest, IntrinsicCyclicAddRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <besm6.h>
        void program()
        {
            unsigned all = 07777777777777777u;   /* 48 one-bits */
            printf("%d %d\n", __besm6_arx(all, 1), __besm6_arx(2, 3));
        }
    )");
    EXPECT_EQ("1 5\n", result);
}

//
// The ω correction, executed.  Both globals are zero, so the cyclic add yields zero and the
// `if` must not be taken.  Without the `aex` the `uza` inherits multiplicative ω from the
// arx, tests bit 48 of a zero accumulator, finds it clear — and takes the branch, printing
// NONZERO.  Suppressing rule #33 must flip this test.
//
TEST_F(CodegenTest, IntrinsicCyclicAddBranchRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <besm6.h>
        unsigned ga, gb;
        void program()
        {
            if (__besm6_arx(ga, gb))
                puts("NONZERO");
            else
                puts("ZERO");
            gb = 1;
            if (__besm6_arx(ga, gb))
                puts("NONZERO");
            else
                puts("ZERO");
        }
    )");
    EXPECT_EQ(
        "ZERO\n"
        "NONZERO\n",
        result);
}

//
// Execution — Unix (b6as/b6ld/b6sim).  Entry point is main(), and b6sim's output is not
// case-folded, so the expectations are verbatim.
//
TEST_F(CodegenTest, UnixRunIntrinsics)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        #include <besm6.h>
        unsigned ga, gb;
        int main(void)
        {
            unsigned a = 0525252525252525u;
            unsigned m = 0770077007700770u;
            printf("%d %d\n", __besm6_acx(0377, 0), __besm6_anx(1, 0));
            printf("%d\n", __besm6_aux(__besm6_apx(a, m), m) == (a & m));
            printf("%d\n", __besm6_arx(07777777777777777u, 1));
            if (__besm6_arx(ga, gb))
                puts("nonzero");
            else
                puts("zero");
            return 0;
        }
    )");
    EXPECT_EQ(
        "8 48\n"
        "1\n"
        "1\n"
        "zero\n",
        result);
}

//
// __besm6_stop — the halt (033, Format 2).  Instruction selection in all three dialects.
//
// The halt code rides in the instruction's own 15-bit address field, which is why it must be a
// compile-time constant; the field is rendered in decimal, so the octal 0377 below comes out as
// 255.  Note what is *absent*: no `,call,`, and no `,subp, __besm6_stop` — the intrinsic is a
// call in the IR and never a symbol in the object.
//
// Madlen is the odd one out: it has no `stop` mnemonic at all, so the halt goes out as the raw
// octal machine code `,33,` — two digits selecting the Format-2 opcode 033 (three digits,
// `,033,`, would be the Format-1 033, i.e. `ext`, which faults).  Bemsh and b6as spell it
// `стоп` / `stop`.
//
TEST_F(CodegenTest, IntrinsicStopMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        void bye(void) { __besm6_stop(0377); }
    )");
    EXPECT_EQ(R"(c
      bye:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,33, 255
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// The halt is RESUMABLE — the operator presses continue on the console and execution goes on at
// the next instruction — so `stop` is not a terminator: it is an ordinary call that returns, the
// function keeps its `,uj, b/ret` epilogue (above), and the code after the halt is live.
//
// This is the test that guards peephole rule #31(b): `stop` must not open an unreachable run.  If
// it did, everything from the halt to the next label would be deleted and the second `,call, puts`
// below would vanish.
//
TEST_F(CodegenTest, IntrinsicStopResumableMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <stdio.h>
        #include <besm6.h>
        void f(void)
        {
            puts("A");
            __besm6_stop(5);
            puts("B");
        }
    )");
    EXPECT_EQ(R"(c
        f:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
          14 ,vtm, *str0
             ,ita, 14
             ,aox, =:64
          14 ,vtm, -1
             ,call, puts
             ,33, 5
          14 ,vtm, *str1
             ,ita, 14
             ,aox, =:64
          14 ,vtm, -1
             ,call, puts
             ,uj, b/ret
    *str1:   ,log, 2040000000000000
    *str0:   ,log, 2020000000000000
             ,end,
)",
              output);
}

TEST_F(CodegenTest, IntrinsicStopUnix)
{
    std::string output = CompileToUnix(R"(
        #include <besm6.h>
        void bye(void) { __besm6_stop(5); }
    )");
    EXPECT_EQ(R"(    .text
    .globl bye
bye:
    its 13
 13 vjm b$save0
    stop 5
    uj b$ret
)",
              output);
}

TEST_F(CodegenTest, IntrinsicStopBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        void bye(void) { __besm6_stop(5); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
bye    старт 1
_save0 внешн ._save0
_ret   внешн ._ret
       счим 13
       пв _save0(13)
       стоп 5
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

//
// The halt, executed.  Real hardware stops and can be resumed from the console; both of our
// simulators instead treat opcode 0330 as "the run is over" and end it, ignoring the halt code.
// So BEFORE — already flushed, since putbyte flushes on '\n' — is printed and AFTER is not, even
// though IntrinsicStopResumableMadlen proves the code for it was emitted.  The job still ends
// cleanly: dubna prints its usual footer and exits 0.
//
TEST_F(CodegenTest, IntrinsicStopRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <besm6.h>
        void program()
        {
            puts("BEFORE");
            __besm6_stop(5);
            puts("AFTER");
        }
    )");
    EXPECT_EQ("BEFORE\n", result);
}

// The same halt through the Bemsh translator (стоп 5) — the one assembler in the chain whose
// source we cannot read, so this is what proves it accepts an operand on стоп.
TEST_F(CodegenTest, BemshIntrinsicStopRun)
{
    std::string result = CompileAndRunBemsh(R"(
        #include <stdio.h>
        #include <besm6.h>
        void program()
        {
            puts("BEFORE");
            __besm6_stop(5);
            puts("AFTER");
        }
    )");
    EXPECT_EQ("BEFORE\n", result);
}

// And through the Unix path: b6as assembles `stop 5`, b6ld links with no __besm6_stop symbol to
// resolve, and b6sim halts on it and exits 0.
TEST_F(CodegenTest, UnixRunIntrinsicStop)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunUnix(R"(
        #include <stdio.h>
        #include <besm6.h>
        int main(void)
        {
            puts("before");
            __besm6_stop(5);
            puts("after");
            return 0;
        }
    )");
    EXPECT_EQ("before\n", result);
}

//
// __besm6_getpsw / __besm6_setpsw / __besm6_maskpsw — Tier 1, the mode word PSW, machine
// register 021 of the register file.  `ita`/`ati` name it through their address field just as
// they name an index register, so the octal 021 comes out as the decimal 17 every assembler
// wants; `__besm6_maskpsw` is a `vtm` whose *modifier register is 0*, which is what makes it a
// PSW write instead of an index-register load.
//
// Madlen will not spell that last one: `,vtm,` with a zero modifier — omitted or written out —
// is rejected with "ошибка в модификаторе" (verified on dubna; it takes only 1-15, for `,utm,`
// too), so the mode write goes out as the raw octal Format-2 opcode `,24,`, exactly as the halt
// goes out as `,33,`.  That the raw form is the same instruction was verified two ways: Bemsh's
// own translator encodes `уиа 1027` as `00 24 02003`, and dubna executes `,24, 4` as the trace
// toggle it gives a register-0 `vtm`.
//
// There is no run test, and there cannot be one.  Neither simulator models the register file
// past the index registers: dubna and b6sim both compute `M[Aex & 017]`, so `ita 021` would read
// M[1] and `ati 021` would *clobber* it — an ABI-preserved register — and both give a register-0
// `vtm` a private meaning (the instruction-trace toggle).  As with ext/mod, what is verified
// mechanically is that every assembler accepts what we emit.
//
TEST_F(CodegenTest, IntrinsicPswMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        int  rd(void)  { return __besm6_getpsw(); }
        void wr(int p) { __besm6_setpsw(p); }
        void cli(void) { __besm6_maskpsw(02003); }
    )");
    EXPECT_EQ(R"(c
       rd:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,ita, 17
             ,uj, b/ret
             ,end,
c
       wr:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,ati, 17
             ,uj, b/ret
             ,end,
c
      cli:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,24, 1027
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// A read-modify-write of the mode word, which is what `setpsw` exists for: the peephole leaves
// the three instructions with nothing between them, since `ita` delivers the value straight to
// the accumulator the `aox` and the `ati` want.
//
TEST_F(CodegenTest, IntrinsicPswReadModifyWriteMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        void raise(int bits) { __besm6_setpsw(__besm6_getpsw() | bits); }
    )");
    EXPECT_EQ(R"(c
    raise:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
             ,ita, 17
           6 ,aox,
             ,ati, 17
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// b6as names the register-0 `vtm` normally — `vtm 1027`, the same instruction Madlen has to
// write as `,24, 1027`, and the same one kernel/psw.s in the sibling v7besm tree writes as
// `vtm 02003`.
//
TEST_F(CodegenTest, IntrinsicPswUnix)
{
    std::string output = CompileToUnix(R"(
        #include <besm6.h>
        int  getpsw(void) { return __besm6_getpsw(); }
        void cli(void)    { __besm6_maskpsw(02003); }
        void sti(void)    { __besm6_maskpsw(3); }
    )");
    EXPECT_EQ(R"(    .text
    .globl getpsw
getpsw:
    its 13
 13 vjm b$save0
    ita 17
    uj b$ret
    .text
    .globl cli
cli:
    its 13
 13 vjm b$save0
    vtm 1027
    uj b$ret
    .text
    .globl sti
sti:
    its 13
 13 vjm b$save0
    vtm 3
    uj b$ret
)",
              output);
}

// Bemsh keeps the mnemonics too: счи / уи / уиа, the last with an empty register field.
TEST_F(CodegenTest, IntrinsicPswBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        void wr(int p) { __besm6_setpsw(p); }
        void cli(void) { __besm6_maskpsw(02003); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
wr     старт 1
_save  внешн ._save
_ret   внешн ._ret
       счим 13
       пв _save(13)
       сч (6)
       уи 17
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
ввд$$$
*
cli    старт 1
_save0 внешн ._save0
_ret   внешн ._ret
       счим 13
       пв _save0(13)
       уиа 1027
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

//
// The mechanical validation: b6as assembles `ita 17` / `ati 17` / `vtm 1027` and b6ld links with
// no `__besm6_getpsw` symbol left to resolve.  (It is not run — see the header above.)
//
TEST_F(CodegenTest, UnixAssembleIntrinsicPsw)
{
    SKIP_IF_NO_UNIX_TOOLS();
    CompileAndAssembleUnix(WrapMain(R"(
#include <besm6.h>
int main(void) {
    int psw = __besm6_getpsw();
    __besm6_setpsw(psw | 02000);
    __besm6_maskpsw(02003);
    __besm6_maskpsw(3);
    return psw & 1;
}
)"));
}

//
// __besm6_ext / __besm6_mod — Tier 1, the machine's only I/O (033 ext, 002 mod, both
// Format 1).  The accumulator is both the input and the output; the *direction* of the
// transfer lives in the address, not in the instruction.
//
// A constant address becomes the instruction's own 12-bit offset field — rendered in
// decimal, like every numeric address field, so the octal 04031 of the peripherals map
// comes out as 2073 (and 036 as 30, 0237 as 159).  Every address in the map fits.
//
// There is no run test: opcodes 002 and 033 are kernel-mode, and both dubna and b6sim throw
// `Illegal instruction` on them.  What *is* verified mechanically is that each assembler
// accepts the mnemonic — b6as by UnixAssembleIntrinsicIo below, and the two Dubna
// translators by hand (`,ext, 2073` and `увв 2073` both assemble to opcode 033).
//
TEST_F(CodegenTest, IntrinsicExtConstMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned ready(void) { return __besm6_ext(04031, 0); }
    )");
    EXPECT_EQ(R"(c
    ready:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta,
             ,ext, 2073
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// A computed address rides in the C address-modifier register, and the instruction reads it
// as EA = 0 + C.  The hardware genuinely needs a computed address: `002 0100`-`0137` (the
// РУУ mode bits) encodes its data *in* the address, and tape-transport control selects the
// unit as addr - 0100.
//
// Here the address is a parameter, so it is already in memory and `wtc` (023, "C = bits 15:1
// of mem[EA]") reads it straight from its frame slot — no index register, and no accumulator
// round-trip.  Instruction selection emits the general stack form and peephole rule #32(b)
// collapses it to this; see IntrinsicExtSumMadlen for the form that survives when the
// address is genuinely computed.
//
TEST_F(CodegenTest, IntrinsicExtComputedMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned io(unsigned addr, unsigned acc) { return __besm6_ext(addr, acc); }
    )");
    EXPECT_EQ(R"(c
       io:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta, 1
           6 ,wtc,
             ,ext,
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// A module-level address variable takes the same single `wtc`, with no `utc` escape ahead of
// it: WTC is a Format 2 instruction, so its own address field is 15 bits and reaches any
// global directly.  (The Format 1 accessors are the ones that need the escape.)
//
TEST_F(CodegenTest, IntrinsicExtGlobalAddrMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned dev;
        unsigned io(unsigned acc) { return __besm6_ext(dev, acc); }
    )");
    EXPECT_EQ(R"(c
      dev:   ,name,
             ,bss, 1
             ,end,
c
       io:   ,name,
    b/ret:   ,subp,
      dev:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,wtc, dev
             ,ext,
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// A constant *added* to an address does not need to be computed at all: it fits the
// instruction's own 12-bit offset field, and `EA = addr + C` adds it back at no cost.  This
// is the tape-transport shape from the peripherals map — unit selection is `addr - 0100`, so
// device code says `unit + 0100` — and both C spellings of the addition fold.
//
// `unit + 0100` on a signed operand is the machine's own `a+x`; `x + 1` on the header's
// `unsigned` parameter is a call to the runtime helper `b$uadd`.  Rule #32(a) deletes either
// and moves the constant into the address field.  That is exact even though the C addition
// is 48-bit and the field is 15: the `wtc` keeps only bits 15:1 and EA is formed mod
// 0100000, and truncation commutes with addition.
//
TEST_F(CodegenTest, IntrinsicExtDisplacementMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned tape(int unit) { return __besm6_ext(unit + 0100, 0); }
        unsigned next(unsigned x) { return __besm6_ext(x + 1, 0); }
    )");
    EXPECT_EQ(R"(c
     tape:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
             ,xta,
           6 ,wtc,
             ,ext, 64
             ,uj, b/ret
             ,end,
c
     next:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
             ,xta,
           6 ,wtc,
             ,ext, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// The general form, when the address really is computed and lives nowhere: XTS (003) pushes
// it and loads the accumulator operand in one instruction, and the `wtc` that follows is in
// stack mode (V = 0, M = 017), so it decrements the stack pointer and pops the address into
// C.  Push and pop are adjacent and balanced, and no index register is disturbed — r14 in
// particular is left alone, which the old lowering could not say.
//
TEST_F(CodegenTest, IntrinsicExtSumMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned io(unsigned a, unsigned b, unsigned acc) { return __besm6_ext(a + b, acc); }
    )");
    EXPECT_EQ(R"(c
       io:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
           6 ,xts, 1
             ,call, b/uadd
           6 ,xts, 2
          15 ,wtc,
             ,ext,
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// A write: the address carries no read bit, so the result is the unchanged accumulator and
// the caller discards it.  The store goes, the instruction stays — these are the machine's
// only I/O and are never eliminable.
//
// The second function dismisses a ГРП interrupt — __besm6_mod(037, ~m), and note the
// complement: 002 037 clears ГРП by writing a mask in which a ZERO bit clears.  A computed
// *accumulator* (not address) stays pure accumulator dataflow — the complement is an `,aex,`
// against the all-ones word, and the ГРП write follows it directly.
//
TEST_F(CodegenTest, IntrinsicModWriteMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        void mask(unsigned m) { __besm6_mod(036, m); }
        void dismiss(unsigned m) { __besm6_mod(037, ~m); }
    )");
    EXPECT_EQ(R"(c
     mask:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,mod, 30
             ,uj, b/ret
             ,end,
c
  dismiss:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,aex, =7777777777777777
             ,mod, 31
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// Reading ГРП is __besm6_mod(0237, 0): a read address (bit 0200), and a zero accumulator
// needs no literal at all — the `,xta,` is left with an empty address field and reads memory
// word 0, which always reads as zero.
//
// The second function's address, 010000, is the one case that does not fit the 12-bit
// Format-1 offset field.  It still needs no computation: `utc` (022) is Format 2, so its own
// 15-bit address field holds the whole value and it sets C from it directly, touching
// neither A nor an index register.  No address in the peripherals map is that large; this is
// what keeps an out-of-range constant correct rather than truncated.
//
TEST_F(CodegenTest, IntrinsicExtLongAddrMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned grp(void) { return __besm6_mod(0237, 0); }
        unsigned far(void) { return __besm6_ext(010000, 0); }
    )");
    EXPECT_EQ(R"(c
      grp:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta,
             ,mod, 159
             ,uj, b/ret
             ,end,
c
      far:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta,
             ,utc, 4096
             ,ext,
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// The ω contract, pinned.  Peephole rules #27+#28 drop the store/reload of the result, so the
// branch consumes the accumulator the `,ext,` itself left — `,ext,` then `,uza,` with nothing
// in between.  That is correct, and needs no correcting `,aox,` (unlike `arx`): a read address
// switches the AU mode register to *logical*, which is the mode compiled code already runs in,
// so the `uza` tests A ≠ 0 and not abs(A) < 0.5.
//
TEST_F(CodegenTest, IntrinsicExtBranchMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <stdio.h>
        #include <besm6.h>
        void poll(void)
        {
            if (__besm6_ext(04031, 0))
                puts("READY");
        }
    )");
    EXPECT_EQ(R"(c
     poll:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save0
             ,xta,
             ,ext, 2073
             ,uza, *3
          14 ,vtm, *str0
             ,ita, 14
             ,aox, =:64
          14 ,vtm, -1
             ,call, puts
             ,uj, *4
       *3:   ,bss,
       *4:   ,bss,
             ,uj, b/ret
    *str0:   ,log, 2444250121054400
             ,end,
)",
              output);
}

//
// Instruction selection — Unix (b6as).  Same two instructions, same Latin mnemonics.
//
TEST_F(CodegenTest, IntrinsicIoUnix)
{
    std::string output = CompileToUnix(R"(
        #include <besm6.h>
        unsigned ready(void) { return __besm6_ext(04031, 0); }
        unsigned io(unsigned addr, unsigned acc) { return __besm6_ext(addr, acc); }
        void mask(unsigned m) { __besm6_mod(036, m); }
    )");
    EXPECT_EQ(R"(    .text
    .globl ready
ready:
    its 13
 13 vjm b$save0
    xta
    ext 2073
    uj b$ret
    .text
    .globl io
io:
    its 13
 13 vjm b$save
  6 xta 1
  6 wtc
    ext
    uj b$ret
    .text
    .globl mask
mask:
    its 13
 13 vjm b$save
  6 xta
    mod 30
    uj b$ret
)",
              output);
}

//
// Instruction selection — Bemsh.  увв = ext (033) and рег = mod (002); мод = wtc (023), the
// C-register load whose own modifier register is parenthesized after the address, so a
// computed address reads `мод (6)` and the I/O op that consumes it needs no operand at all.
//
TEST_F(CodegenTest, IntrinsicExtConstBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        unsigned ready(void) { return __besm6_ext(04031, 0); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
ready  старт 1
_save0 внешн ._save0
_ret   внешн ._ret
       счим 13
       пв _save0(13)
       сч
       увв 2073
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

TEST_F(CodegenTest, IntrinsicModComputedBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        unsigned io(unsigned addr, unsigned acc) { return __besm6_mod(addr, acc); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
io     старт 1
_save  внешн ._save
_ret   внешн ._ret
       счим 13
       пв _save(13)
       сч 1(6)
       мод (6)
       рег
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

//
// The one mechanical validation available: b6as assembles `ext 2073` / ` 14 ext` / `mod 30`,
// and b6ld links with no `__besm6_ext` symbol left to resolve — the intrinsic is a call in the
// IR and never a symbol in the object.  (Running it is impossible: b6sim, like dubna, throws
// `Illegal instruction` on a user-mode 002/033.)
//
TEST_F(CodegenTest, UnixAssembleIntrinsicIo)
{
    SKIP_IF_NO_UNIX_TOOLS();
    CompileAndAssembleUnix(WrapMain(R"(
#include <besm6.h>
int main(void) {
    unsigned ready = __besm6_ext(04031, 0);
    unsigned grp = __besm6_mod(0237, 0);
    __besm6_mod(036, 0);
    __besm6_mod(037, ~1u);
    return (int)(__besm6_ext(ready & 07777, grp) & 1);
}
)"));
}

//
// Execution — Bemsh / dubna (the Cyrillic mnemonics, assembled by the Bemsh translator and
// linked against libbem.bin).  Upper case again, for the same reason as the Madlen path.
//
TEST_F(CodegenTest, BemshIntrinsicsRun)
{
    std::string result = CompileAndRunBemsh(R"(
        #include <stdio.h>
        #include <besm6.h>
        unsigned ga, gb;
        void program()
        {
            unsigned a = 0525252525252525u;
            unsigned m = 0770077007700770u;
            printf("%d %d\n", __besm6_acx(0377, 0), __besm6_anx(1, 0));
            printf("%d\n", __besm6_aux(__besm6_apx(a, m), m) == (a & m));
            printf("%d\n", __besm6_arx(07777777777777777u, 1));
            if (__besm6_arx(ga, gb))
                puts("NONZERO");
            else
                puts("ZERO");
        }
    )");
    EXPECT_EQ(
        "8 48\n"
        "1\n"
        "1\n"
        "ZERO\n",
        result);
}

//
// __besm6_extracode(op, ea, acc) — Tier 3.  The user-mode trap into the operating system:
// M[016] := EA, A := acc, invoke the extracode, result := A.  The Unix v7 syscall trap
// `$77 N` rides on exactly this mechanism, which is what would let libc's hand-written
// syscall leaves (write.s, read.s, exit.s) be written in C.
//
// `op` *is* the opcode, so it is not an operand at all: it becomes the mnemonic, and each
// dialect spells that differently — Madlen `,*77,`, Bemsh `э77`, b6as `$77` (a raw octal
// opcode; b6as names no mnemonic for 050-077).  The effective address is an ordinary
// Format-1 address field, rendered in decimal like every other numeric address.
//
TEST_F(CodegenTest, IntrinsicExtracodeMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        void bye(unsigned code) { __besm6_extracode(077, 1, code); }
    )");
    EXPECT_EQ(R"(c
      bye:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta,
             ,*77, 1
             ,uj, b/ret
             ,end,
)",
              output);
}

//
// A computed effective address goes through the scratch index register r14, exactly as a
// computed device address does for ext/mod: EA = M[14] + 0.  The accumulator is loaded last,
// because materializing the address clobbers it.
//
TEST_F(CodegenTest, IntrinsicExtracodeComputedMadlen)
{
    std::string output = CompileToMadlen(R"(
        #include <besm6.h>
        unsigned trap(unsigned ea, unsigned acc) { return __besm6_extracode(070, ea, acc); }
    )");
    EXPECT_EQ(R"(c
     trap:   ,name,
    b/ret:   ,subp,
             ,its, 13
             ,call, b/save
           6 ,xta, 1
           6 ,wtc,
             ,*70,
             ,uj, b/ret
             ,end,
)",
              output);
}

TEST_F(CodegenTest, IntrinsicExtracodeUnix)
{
    std::string output = CompileToUnix(R"(
        #include <besm6.h>
        void bye(unsigned code) { __besm6_extracode(077, 1, code); }
        unsigned trap(unsigned ea, unsigned acc) { return __besm6_extracode(070, ea, acc); }
    )");
    EXPECT_EQ(R"(    .text
    .globl bye
bye:
    its 13
 13 vjm b$save
  6 xta
    $77 1
    uj b$ret
    .text
    .globl trap
trap:
    its 13
 13 vjm b$save
  6 xta 1
  6 wtc
    $70
    uj b$ret
)",
              output);
}

TEST_F(CodegenTest, IntrinsicExtracodeBemsh)
{
    std::string output = CompileToBemsh(R"(
        #include <besm6.h>
        void bye(unsigned code) { __besm6_extracode(077, 1, code); }
    )");
    EXPECT_EQ(R"(ввд$$$
*
bye    старт 1
_save  внешн ._save
_ret   внешн ._ret
       счим 13
       пв _save(13)
       сч (6)
       э77 1
       пб _ret
       финиш
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
)",
              output);
}

//
// Execution — Madlen / dubna.  Extracode 074 is the monitor's "finish" (a legal halt), so the
// program ends inside the intrinsic: HELLO is printed, WORLD never is.  That the run reaches
// the halt at all is the point — it proves the Madlen translator both accepts `,*74,` (a
// symbolic mnemonic, unlike the halt's raw octal `,33,`) and executes it as an extracode.
//
TEST_F(CodegenTest, IntrinsicExtracodeRun)
{
    std::string result = CompileAndRun(R"(
        #include <stdio.h>
        #include <besm6.h>
        void program()
        {
            puts("HELLO");
            __besm6_extracode(074, 0, 0);
            puts("WORLD");
        }
    )");
    EXPECT_EQ("HELLO\n", result);
}

//
// The same finish extracode on the Bemsh path, spelled `э74`.
//
TEST_F(CodegenTest, BemshIntrinsicExtracodeRun)
{
    std::string result = CompileAndRunBemsh(R"(
        #include <stdio.h>
        #include <besm6.h>
        void program()
        {
            puts("HELLO");
            __besm6_extracode(074, 0, 0);
            puts("WORLD");
        }
    )");
    EXPECT_EQ("HELLO\n", result);
}

//
// b6as assembles both forms — the immediate `$77 1` and the r14-modified ` 14 $70` — and b6ld
// links with no `__besm6_extracode` symbol left to resolve: the intrinsic is a call in the IR
// and never a symbol in the object.
//
TEST_F(CodegenTest, UnixAssembleIntrinsicExtracode)
{
    SKIP_IF_NO_UNIX_TOOLS();
    CompileAndAssembleUnix(WrapMain(R"(
#include <besm6.h>
unsigned trap(unsigned ea, unsigned acc) { return __besm6_extracode(070, ea, acc); }
int main(void) {
    __besm6_extracode(077, 1, 0);
    return (int)trap(1, 2);
}
)"));
}

//
// Execution — Unix (b6as/b6ld/b6sim).  `$77 1` is SYS_exit, whose status the simulator takes
// from the accumulator alone: the one v7 syscall whose ABI needs no stack arguments, so a
// plain C call site can issue it.  The code before the trap runs (BYE), the code after it does
// not, and b6sim's --status prints the status the accumulator carried.
//
TEST_F(CodegenTest, UnixRunIntrinsicExtracode)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunBook(R"(
        #include <stdio.h>
        #include <besm6.h>
        int main(void)
        {
            puts("BYE");
            __besm6_extracode(077, 1, 42);
            return 7;
        }
    )");
    EXPECT_EQ(
        "BYE\n"
        "42\n",
        result);
}

//
// The same trap with a *computed* effective address, so the C-register lowering is executed
// rather than only pattern-matched.  `base + 1` with base = 0 is syscall 1 again, and the
// exit code still arrives in A — but this reaches the trap through peephole rule #32(a)
// (the displacement folds into the instruction's own address field) and #32(b) (the base
// comes straight out of its frame slot via `wtc`), i.e. through the three-instruction form.
//
TEST_F(CodegenTest, UnixRunIntrinsicExtracodeComputed)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunBook(R"(
        #include <stdio.h>
        #include <besm6.h>
        void leave(unsigned base, unsigned code)
        {
            __besm6_extracode(077, base + 1, code);
        }
        int main(void)
        {
            puts("BYE");
            leave(0, 42);
            return 7;
        }
    )");
    EXPECT_EQ(
        "BYE\n"
        "42\n",
        result);
}

//
// And once more through the shape nothing folds: the effective address is the sum of two
// runtime values, so it exists only in the accumulator and reaches C by the general route —
// XTS pushes it while loading the code, and the stack-mode `wtc` pops it back.  This is the
// test that the push/pop pair is balanced and lands the right word in C.
//
TEST_F(CodegenTest, UnixRunIntrinsicExtracodeStacked)
{
    SKIP_IF_NO_UNIX_RUN_TOOLS();
    std::string result = CompileAndRunBook(R"(
        #include <stdio.h>
        #include <besm6.h>
        void leave(unsigned a, unsigned b, unsigned code)
        {
            __besm6_extracode(077, a + b, code);
        }
        int main(void)
        {
            puts("BYE");
            leave(0, 1, 42);
            return 7;
        }
    )");
    EXPECT_EQ(
        "BYE\n"
        "42\n",
        result);
}
