# MMIX standard library headers

The C11 headers that depend on MMIX's data model: `float.h`, `limits.h`, `setjmp.h`,
`stdarg.h`, `stddef.h` and `stdint.h`.  `inttypes.h` and
`math.h` describe the LP64 data model, in [libc/lp64/include](../../lp64/include),
searched second; the target-neutral ones are in
[libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/mmix/include -I libc/lp64/include -I libc/common/include prog.c prog.i
```

`int` is 32 bits, `long`, `long long` and pointers 64, `short` 16; `size_t` is
`unsigned long`, `ptrdiff_t` is `long`, `wchar_t` is `int`, `wint_t` `unsigned int`, and
plain `char` is signed: the types are mmix-knuth-mmixware-gcc's, which the backend's ABI
follows.  Every type is aligned to its size.  `float` is IEEE binary32, `double` and
`long double` binary64, in hardware.  A variadic function stores its argument registers
below the incoming stack arguments, so every variable argument is one 8-byte slot, its
value right-justified: `va_list` is a `char *`, as GCC's is.  `jmp_buf` is newlib's
five `unsigned long`s, the layout of GCC's built-in, and `setjmp`/`longjmp` are newlib's
(`libc/mmix/setjmp.s`).  `HeadersAgreeWithGcc` (`backend/mmix/test/interop_tests.cpp`)
checks the sizes, limits, type identities and `float.h` values against GCC's own.

`limits.h` is MMIX's own rather than the LP64 one keyed off `__CHAR_UNSIGNED__`: the
build and the test fixtures preprocess with the host's `cc -E`, which defines that macro
by the host's own `char`.  It is the same as x86-64's, the other LP64 target with a
signed `char`.  See [backend/mmix/Plan.md](../../../backend/mmix/Plan.md).
