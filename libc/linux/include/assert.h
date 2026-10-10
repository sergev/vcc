/*
 * <assert.h> — diagnostics (C11 §7.2), hosted Linux: glibc's __assert_fail.
 * Not guarded: each inclusion redefines assert by the current NDEBUG.
 */
#undef assert

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else

_Noreturn void __assert_fail(const char *expr, const char *file, unsigned int line,
                             const char *func);

#define assert(expr) ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))

#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif
