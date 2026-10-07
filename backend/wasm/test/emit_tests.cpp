//
// wasm32 IR emitter: the LLVM assembler's syntax of each immediate form.
//
#include <gtest/gtest.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "internal.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the wasm32-tests binary.
extern "C" void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

template <typename F>
static std::string Capture(F write)
{
    FILE *f = tmpfile();
    EXPECT_NE(nullptr, f);
    write(f);
    long len = ftell(f);
    rewind(f);
    std::string s(static_cast<size_t>(len), '\0');
    if (len > 0)
        EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
    fclose(f);
    return s;
}

static Wasm_Func *NewFunc(const char *name, bool global, Wasm_ValType result)
{
    Wasm_Func *fn = wasm_new_func(name, global);
    fn->params =
        static_cast<Wasm_ValType *>(xalloc(sizeof(Wasm_ValType), __func__, __FILE__, __LINE__));
    fn->params[0] = WASM_I32;
    fn->nparams   = 1;
    fn->result    = result;
    return fn;
}

static std::string Emit(Wasm_Func *fn)
{
    std::string s = Capture([&](FILE *f) { wasm_emit_func(f, fn); });
    wasm_free_func(fn);
    return s;
}

static uint64_t Bits(double d)
{
    uint64_t b;
    memcpy(&b, &d, sizeof(b));
    return b;
}

static uint32_t Bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof(b));
    return b;
}

// The header, the locals, and every immediate form.
TEST(WasmEmit, Forms)
{
    Wasm_Func *fn = NewFunc("f", true, WASM_I32);
    EXPECT_EQ(1, wasm_add_local(fn, WASM_I64));
    EXPECT_EQ(2, wasm_add_local(fn, WASM_F64));
    wasm_append(fn, WASM_LOCAL_GET)->imm = 0;
    wasm_append(fn, WASM_I32_CONST)->imm = -5;
    wasm_append(fn, WASM_I32_ADD);
    wasm_append(fn, WASM_I64_CONST)->imm = 1099511627776LL;
    wasm_append(fn, WASM_DROP);
    wasm_append(fn, WASM_F32_CONST)->imm = Bits(1.5f);
    wasm_append(fn, WASM_F64_CONST)->imm = (int64_t)Bits(-HUGE_VAL);
    Wasm_Instr *in                       = wasm_append(fn, WASM_I32_CONST);
    in->sym                              = xstrdup("g");
    in->imm                              = 8;
    in                                   = wasm_append(fn, WASM_I32_LOAD);
    in->imm                              = 4;
    in->align                            = 1;
    in                                   = wasm_append(fn, WASM_I64_STORE);
    in->sym                              = xstrdup("h");
    wasm_append(fn, WASM_BLOCK);
    wasm_append(fn, WASM_LOOP)->bt = WASM_I32;
    in                             = wasm_append(fn, WASM_BR_TABLE);
    in->table    = static_cast<int *>(xalloc(2 * sizeof(int), __func__, __FILE__, __LINE__));
    in->table[0] = 0;
    in->table[1] = 1;
    in->ntable   = 2;
    wasm_append(fn, WASM_END_LOOP);
    wasm_append(fn, WASM_BR_IF)->imm = 0;
    wasm_append(fn, WASM_END_BLOCK);
    wasm_append(fn, WASM_CALL)->sym          = xstrdup("putch");
    wasm_append(fn, WASM_CALL_INDIRECT)->sym = xstrdup("(i32) -> ()");
    wasm_append(fn, WASM_MEMORY_COPY);
    wasm_append(fn, WASM_MEMORY_GROW);
    wasm_append(fn, WASM_GLOBAL_GET)->sym = xstrdup("__stack_pointer");
    wasm_append(fn, WASM_RETURN);
    EXPECT_EQ(
        "\t.section\t.text.f,\"\",@\n"
        "\t.globl\tf\n"
        "\t.type\tf,@function\n"
        "f:\n"
        "\t.functype\tf (i32) -> (i32)\n"
        "\t.local\ti64, f64\n"
        "\tlocal.get\t0\n"
        "\ti32.const\t-5\n"
        "\ti32.add\n"
        "\ti64.const\t1099511627776\n"
        "\tdrop\n"
        "\tf32.const\t0x1.8p+0\n"
        "\tf64.const\t-infinity\n"
        "\ti32.const\tg+8\n"
        "\ti32.load\t4:p2align=0\n"
        "\ti64.store\th\n"
        "\tblock\n"
        "\tloop\ti32\n"
        "\tbr_table\t{0, 1}\n"
        "\tend_loop\n"
        "\tbr_if\t0\n"
        "\tend_block\n"
        "\tcall\tputch\n"
        "\tcall_indirect\t__indirect_function_table, (i32) -> ()\n"
        "\tmemory.copy\t0, 0\n"
        "\tmemory.grow\t0\n"
        "\tglobal.get\t__stack_pointer\n"
        "\treturn\n"
        "\tend_function\n",
        Emit(fn));
}

// A static function is not .globl; one returning nothing has no result.
TEST(WasmEmit, StaticVoid)
{
    Wasm_Func *fn = NewFunc("s", false, WASM_VOID);
    EXPECT_EQ(
        "\t.section\t.text.s,\"\",@\n"
        "\t.type\ts,@function\n"
        "s:\n"
        "\t.functype\ts (i32) -> ()\n"
        "\tend_function\n",
        Emit(fn));
}

// Control that can run off the end of a function with a result reaches an unreachable.
TEST(WasmEmit, UnreachableAtEnd)
{
    Wasm_Func *fn                        = NewFunc("u", true, WASM_I32);
    wasm_append(fn, WASM_I32_CONST)->imm = 1;
    wasm_append(fn, WASM_DROP);
    std::string s = Emit(fn);
    EXPECT_NE(s.find("\tdrop\n\tunreachable\n\tend_function\n"), std::string::npos) << s;

    fn                                   = NewFunc("r", true, WASM_I32);
    wasm_append(fn, WASM_I32_CONST)->imm = 1;
    wasm_append(fn, WASM_RETURN);
    s = Emit(fn);
    EXPECT_EQ(s.find("unreachable"), std::string::npos) << s;
}

// A NaN keeps its sign; the default quiet NaN is spelled nan, another with its
// significand; an infinity is spelled infinity.
TEST(WasmEmit, FloatSpecials)
{
    Wasm_Func *fn                        = NewFunc("n", true, WASM_VOID);
    wasm_append(fn, WASM_F64_CONST)->imm = (int64_t)Bits(static_cast<double>(NAN));
    wasm_append(fn, WASM_F32_CONST)->imm = Bits(-INFINITY); // a float
    wasm_append(fn, WASM_F64_CONST)->imm = (int64_t)Bits(0.0);
    wasm_append(fn, WASM_F64_CONST)->imm = (int64_t)Bits(-0.0);
    wasm_append(fn, WASM_F64_CONST)->imm = (int64_t)0xfff0000000000123ull;
    std::string s                        = Emit(fn);
    EXPECT_NE(s.find("\tf64.const\tnan\n\tf32.const\t-infinity\n\tf64.const\t0x0p+0\n"
                     "\tf64.const\t-0x0p+0\n\tf64.const\t-nan:0x123\n"),
              std::string::npos)
        << s;
}
