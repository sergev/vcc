/*
 * <assert.h> — diagnostics (C11 §7.2), hosted macOS: libSystem's __assert_rtn.
 * Not guarded: each inclusion redefines assert by the current NDEBUG.
 */
#undef assert

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else

_Noreturn void __assert_rtn(const char *func, const char *file, int line, const char *expr);

#define assert(expr) ((expr) ? (void)0 : __assert_rtn(__func__, __FILE__, __LINE__, #expr))

#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif
