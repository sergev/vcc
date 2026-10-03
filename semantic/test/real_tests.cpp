#include "typecheck_fixture.h"
#include "target.h"

TEST_F(TypecheckTest, SysacctNamei)
{
    ParseProgram(R"(
        struct inode *namei(int (*func)(void), int flag);
        int uchar(void);
        void sysacct()
        {
            namei(uchar, 0);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, SysacctBinaryOp13)
{
    ParseProgram(R"(
        struct inode {
            unsigned short i_mode;
        };
        void sysacct()
        {
            register struct inode *ip;
            if ((ip->i_mode & 0170000) != 0100000) {
                return;
            }
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, SecondLocalVar)
{
    ParseProgram(R"(
        int foo()
        {
            int bar = 0, quz = 0;
            return quz;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, NestedUnionDotRef)
{
    ParseProgram(R"(
        struct foo {
            union {
                int bar;
            } u;
        };
        int quz(struct foo *ptr)
        {
            return ptr->u.bar;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, TypedefFieldArrowAccess)
{
    ParseProgram(R"(
        struct foo { int bar; };
        typedef struct foo *foop;
        int alloc(void *quz)
        {
            return ((foop)quz)->bar;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, CallBcopy)
{
    ParseProgram(R"(
        void bcopy(const void *src, void *dst, unsigned len);
        void copy(const char *from, char *to)
        {
            bcopy(from, to, 42);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, ArgumentCastTypedef)
{
    ParseProgram(R"(
        typedef char *caddr_t;
        void putstr(const char *str);
        void foo(caddr_t str)
        {
            putstr(str);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, StaticVarInitializer)
{
    ParseProgram("static int drv = -1;");
    typecheck_program(program);
}

TEST_F(TypecheckTest, PtrCastAddition)
{
    ParseProgram(R"(
        typedef char *foo;
        foo bar(char *quz)
        {
            return (foo)quz + 2;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, PtrCastDeref)
{
    ParseProgram(R"(
        typedef char *foo;
        char bar(char *quz)
        {
            return *(foo)quz;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, PtrIncrement)
{
    ParseProgram(R"(
        void foo(char *bar)
        {
            bar += 42;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, ArgumentArray)
{
    ParseProgram(R"(
        int foo[42];
        int bar(int[42]);
        void quz()
        {
            bar(foo);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, CallVariadicFunction)
{
    ParseProgram(R"(
        void foo(char *fmt, ...);
        void bar()
        {
            foo("%d", 42);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, CallFunctionByPtr)
{
    ParseProgram(R"(
        void foo(void (*bar)(void))
        {
            (*bar)();
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, FieldAddrViaTypedefStruct)
{
    ParseProgram(R"(
        typedef struct {
            int x;
        } *foo;
        extern int bar;
        int *quz()
        {
            return &((foo)&bar)->x;
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, InitStructOfFuncPtrArgs)
{
    ParseProgram(R"(
        extern struct foo {
            void (*bar)(int);
        } foo[];
        void quz(int);
        struct foo foo[] = {
            { quz },
        };
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, InitStructOfFuncPtrNoArgs)
{
    ParseProgram(R"(
        extern struct foo {
            int (*bar)(int *);
        } foo[];
        int quz();
        struct foo foo[] = {
            { quz },
        };
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, InitFuncPtrNoArgs)
{
    ParseProgram(R"(
        int foo();
        int (*bar)(int *) = foo;
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, ArgumentFuncPtr)
{
    ParseProgram(R"(
        void foo(void (*)(char *));
        void bar(char *quz)
        {
            foo(bar);
        }
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, VarInitSizeof)
{
    ParseProgram("int foo = sizeof(foo);");
    typecheck_program(program);
}

TEST_F(TypecheckTest, VarInitAlignof)
{
    ParseProgram("int foo = _Alignof(int);");
    typecheck_program(program);
}

// __builtin_va_class is an integer constant expression on a target with an argument
// classification (AAPCS64), and rejected on one without.
TEST_F(TypecheckTest, VaClassFolds)
{
    const Target *saved = target_config;
    target_config       = target_lookup("aarch64");
    ParseProgram(R"(
struct hfa { double d[3]; };
int c[__builtin_va_class(struct hfa)];
_Static_assert(__builtin_va_class(struct hfa) == 8 * 8 + 3, "an HFA of three doubles");
_Static_assert(__builtin_va_class(long) == 0, "general registers");
_Static_assert(__builtin_va_class(struct { long a, b, c; }) == 1, "by reference");
)");
    typecheck_program(program);
    target_config = saved;
}

TEST_F(TypecheckTest, VaClassNeedsTarget)
{
    ParseProgram("int c = __builtin_va_class(int);");
    EXPECT_DEATH(typecheck_program(program), "__builtin_va_class is not supported");
}

TEST_F(TypecheckTest, StaticInitShortArray)
{
    ParseProgram("void foo() { static short bar[] = { 1, 2 }; }");
    typecheck_program(program);
}

TEST_F(TypecheckTest, InitVarCastPtrExpression)
{
    ParseProgram("char *foo = (char *)(0x40000000 + 0xe00000);");
    typecheck_program(program);
}

TEST_F(TypecheckTest, InitStructOfPtr)
{
    ParseProgram(R"(
        extern struct foo {
            int *bar;
        } foo[];
        extern int quz;
        struct foo foo[] = {
            { &quz },
        };
    )");
    typecheck_program(program);
}

TEST_F(TypecheckTest, InitArray2D)
{
    ParseProgram(R"(
        char foo[3][1] = {
            { 11 },
            { 22 },
            { 33 },
        };
    )");
    typecheck_program(program);
}

// long long literal with LL suffix typechecks without error.
TEST_F(TypecheckTest, LongLongLiteralLL)
{
    ParseProgram("long long x = 9999999999LL;");
    typecheck_program(program);
}

// unsigned long long literal with ULL suffix typechecks without error.
TEST_F(TypecheckTest, ULongLongLiteralULL)
{
    ParseProgram("unsigned long long x = 18446744073709551615ULL;");
    typecheck_program(program);
}

// long literal with L suffix typechecks without error.
TEST_F(TypecheckTest, LongLiteralL)
{
    ParseProgram("long x = 1000000000L;");
    typecheck_program(program);
}

// Zero-initializing a long long variable works (was hitting fatal_error before fix).
TEST_F(TypecheckTest, LongLongZeroInit)
{
    ParseProgram("static long long x;");
    typecheck_program(program);
}

// Static long long initializer with large value preserves 64 bits.
TEST_F(TypecheckTest, StaticLongLongInit)
{
    ParseProgram("static long long x = 9999999999LL;");
    typecheck_program(program);
}
