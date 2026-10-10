//
// The BESM-6 compiler intrinsics declared in <besm6.h> (backend/besm6/Besm6_Intrinsics.md).
//
// The back end lowers them inline (backend/besm6/intrinsics.c, covered by
// backend/besm6/test/intrinsics_tests.cpp); these tests stop at typecheck and
// pin the contract the header establishes — the names, the signatures, the
// argument types the prototype checks against, the _Noreturn-ness of
// __besm6_stop, and the constant-folded opcode of __besm6_extracode.
//
#include "target.h"
#include "typecheck_fixture.h"

// The immediate-argument checks belong to the BESM-6 target descriptor.
class Besm6IntrinsicsTest : public PipelineTest {
protected:
    const Target *saved = nullptr;
    void SetUp() override
    {
        PipelineTest::SetUp();
        saved         = target_config;
        target_config = target_lookup("besm6");
    }
    void TearDown() override
    {
        target_config = saved;
        PipelineTest::TearDown();
    }
};

// Every intrinsic, called once.
TEST_F(Besm6IntrinsicsTest, Besm6IntrinsicsCallAll)
{
    RunPipeline(R"(#include <besm6.h>
unsigned poke(unsigned a, unsigned m)
{
    unsigned ready = __besm6_ext(04031, 0);
    unsigned grp   = __besm6_mod(0237, 0);
    unsigned bits  = __besm6_apx(a, m);
    unsigned back  = __besm6_aux(bits, m);
    unsigned ones  = __besm6_acx(a, 0);
    unsigned top   = __besm6_anx(a, 0);
    unsigned sum   = __besm6_arx(ready, grp);
    unsigned code  = __besm6_extracode(077, 1, 0);

    unsigned n = __besm6_acx(back, 0) + __besm6_anx(sum, 0) + __besm6_mod(0237, 0);
    __besm6_mod(036, m);
    __besm6_mod(037, ~(unsigned)1);

    __besm6_setpsw(__besm6_getpsw() | 02000);
    __besm6_maskpsw(02003);

    if (n + ones + top + code == 0)
        __besm6_stop(5);
    return n;
})");

    const Symbol *ext = symtab_get("__besm6_ext");
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext->kind, SYM_FUNC);
    ASSERT_NE(ext->type, nullptr);
    EXPECT_EQ(ext->type->kind, TYPE_FUNCTION);
    EXPECT_FALSE(ext->u.func.defined);
    EXPECT_FALSE(ext->u.func.noret);

    const Symbol *stop = symtab_get("__besm6_stop");
    ASSERT_NE(stop, nullptr);
    EXPECT_EQ(stop->kind, SYM_FUNC);
    EXPECT_FALSE(stop->u.func.noret);
}

// A machine word is carried as unsigned, never as int: a signed int on this
// target holds only 41 of the 48 bits, so ГРП bit 48 would not survive it.
TEST_F(Besm6IntrinsicsTest, Besm6IntrinsicWordIsUnsigned)
{
    RunPipeline(R"(#include <besm6.h>
unsigned grp(void)
{
    return __besm6_mod(0237, 0);
})");

    const Symbol *mod = symtab_get("__besm6_mod");
    ASSERT_NE(mod, nullptr);
    ASSERT_NE(mod->type, nullptr);
    ASSERT_EQ(mod->type->kind, TYPE_FUNCTION);

    const Type *ret = mod->type->u.function.return_type;
    ASSERT_NE(ret, nullptr);
    EXPECT_EQ(ret->kind, TYPE_UINT);

    const Param *acc = mod->type->u.function.params;
    ASSERT_NE(acc, nullptr);
    ASSERT_NE(acc->next, nullptr);
    ASSERT_NE(acc->next->type, nullptr);
    EXPECT_EQ(acc->next->type->kind, TYPE_UINT);
}

// The three PSW intrinsics are the deliberate exception to the rule above: they are typed
// `int`, because what they carry is not a 48-bit machine word but a 15-bit address-field
// value — PSW is read and written through `ita`/`ati`/`vtm`, all of which are 15-bit paths.
// __besm6_getpsw is also the one intrinsic that takes no arguments at all.
TEST_F(Besm6IntrinsicsTest, Besm6PswIsInt)
{
    RunPipeline(R"(#include <besm6.h>
int level(void)
{
    int psw = __besm6_getpsw();
    __besm6_setpsw(psw);
    __besm6_maskpsw(02003);
    return psw & 02000;
})");

    const Symbol *get = symtab_get("__besm6_getpsw");
    ASSERT_NE(get, nullptr);
    ASSERT_NE(get->type, nullptr);
    ASSERT_EQ(get->type->kind, TYPE_FUNCTION);
    ASSERT_NE(get->type->u.function.return_type, nullptr);
    EXPECT_EQ(get->type->u.function.return_type->kind, TYPE_INT);
    EXPECT_EQ(get->type->u.function.params, nullptr); // (void)

    const Symbol *set = symtab_get("__besm6_setpsw");
    ASSERT_NE(set, nullptr);
    ASSERT_NE(set->type, nullptr);
    ASSERT_NE(set->type->u.function.return_type, nullptr);
    EXPECT_EQ(set->type->u.function.return_type->kind, TYPE_VOID);
    ASSERT_NE(set->type->u.function.params, nullptr);
    ASSERT_NE(set->type->u.function.params->type, nullptr);
    EXPECT_EQ(set->type->u.function.params->type->kind, TYPE_INT);

    const Symbol *mask = symtab_get("__besm6_maskpsw");
    ASSERT_NE(mask, nullptr);
    ASSERT_NE(mask->type, nullptr);
    ASSERT_NE(mask->type->u.function.params, nullptr);
    ASSERT_NE(mask->type->u.function.params->type, nullptr);
    EXPECT_EQ(mask->type->u.function.params->type->kind, TYPE_INT);
}

// The halt is RESUMABLE: the operator presses continue on the console and the
// machine goes on at the next instruction.  So __besm6_stop is deliberately NOT
// _Noreturn — it is an ordinary void call, the code after it is reachable, and a
// non-void function containing one still has to return a value.
TEST_F(Besm6IntrinsicsTest, Besm6StopIsResumable)
{
    RunPipeline(R"(#include <besm6.h>
int panic(int code)
{
    __besm6_stop(1);
    return code;
})");
    EXPECT_NE(program, nullptr);

    const Symbol *stop = symtab_get("__besm6_stop");
    ASSERT_NE(stop, nullptr);
    EXPECT_FALSE(stop->u.func.noret);
}

// The prototype is enforced: the front end checks arity against it.
TEST_F(Besm6IntrinsicsTest, Besm6IntrinsicWrongArity_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(#include <besm6.h>
unsigned f(unsigned a)
{
    return __besm6_acx(a);
})"),
                 "too (many|few) arguments");
}

// __besm6_extracode's opcode *is* the instruction's opcode, so it must be a compile-time
// constant — the one intrinsic argument the front end has to look at.  A non-constant is
// diagnosed here rather than left to miscompile in the back end.
TEST_F(Besm6IntrinsicsTest, Besm6ExtracodeOpNotConstant_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(#include <besm6.h>
unsigned trap(int op, unsigned ea)
{
    return __besm6_extracode(op, ea, 0);
})"),
                 "must be a constant");
}

// Only 050..077 are extracodes; anything else names a different instruction entirely.
TEST_F(Besm6IntrinsicsTest, Besm6ExtracodeOpOutOfRange_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(#include <besm6.h>
unsigned trap(unsigned ea)
{
    return __besm6_extracode(0100, ea, 0);
})"),
                 "not an extracode");
}

// A constant *expression* is fine, and is folded at typecheck: the argument reaches the back
// end as a literal whatever the optimizer does with it.  (The v7 write syscall is $77 4.)
TEST_F(Besm6IntrinsicsTest, Besm6ExtracodeOpConstantExpr)
{
    RunPipeline(R"(#include <besm6.h>
enum { SYSCALL = 070 };
unsigned wr(unsigned n)
{
    return __besm6_extracode(SYSCALL + 7, 4, n);
})");
    EXPECT_NE(program, nullptr);
}

// __besm6_maskpsw's mask and __besm6_stop's halt code are immediate fields too, and are folded
// by the same front-end pass.  A DEEPLY NESTED constant expression must fold — this is how a
// kernel spells a mode-word mask, out of the named PSW bits rather than as one magic octal.
//
// The regression this pins: the fold used to happen at TAC level in the back end and collapsed
// only ONE level, so a three-term OR arrived as a live node and was rejected as "not a
// constant".  Two terms folded, three did not, which made the rule look arbitrary at the call
// site.  eval_const() is recursive, so nesting depth is no longer a property anyone has to know.
TEST_F(Besm6IntrinsicsTest, Besm6MaskpswNestedConstantExpr)
{
    RunPipeline(R"(#include <besm6.h>
#define PSW_MMAP_DISABLE 00001
#define PSW_PROT_DISABLE 00002
#define PSW_INTR_DISABLE 02000
#define PSW_KERNEL (PSW_MMAP_DISABLE | PSW_PROT_DISABLE)
static int curipl = 1;
int setipl(int s)
{
    int old = curipl;
    curipl = s;
    if (s)
        __besm6_maskpsw(PSW_KERNEL | PSW_INTR_DISABLE);
    else
        __besm6_maskpsw(PSW_KERNEL);
    return old;
}
void halt(void)
{
    __besm6_stop((1 << 3) | (1 << 1) | 1);
})");
    EXPECT_NE(program, nullptr);
}

// The constant requirement itself still holds: the mask is part of the encoding, and there is
// no register to put it in.
TEST_F(Besm6IntrinsicsTest, Besm6MaskpswNotConstant_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(#include <besm6.h>
void spl(int mask)
{
    __besm6_maskpsw(mask);
})"),
                 "must be a constant");
}

// ... and so does the 15-bit range of the address field it rides in.
TEST_F(Besm6IntrinsicsTest, Besm6MaskpswOutOfRange_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(#include <besm6.h>
void spl(void)
{
    __besm6_maskpsw(0100000);
})"),
                 "does not fit the 15-bit address field");
}

TEST_F(Besm6IntrinsicsTest, Besm6StopCodeNotConstant_Neg)
{
    EXPECT_DEATH(RunPipeline(R"(#include <besm6.h>
void die(int code)
{
    __besm6_stop(code);
})"),
                 "must be a constant");
}

// Another target has no immediate-argument intrinsics: the same call is an ordinary one.
TEST_F(PipelineTest, Besm6ImmediateArgsAreTargetSpecific)
{
    RunPipeline(R"(void __besm6_stop(unsigned code);
void f(unsigned x) { __besm6_stop(x); })");
}
