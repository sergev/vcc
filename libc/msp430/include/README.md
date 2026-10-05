# MSP430 standard library headers

The C11 headers that depend on MSP430's data model: `float.h`, `limits.h`, `math.h`,
`setjmp.h`, `stdarg.h`, `stddef.h` and `stdint.h`.  `inttypes.h` describes the 16-bit data model
MSP430 shares with AVR, in [libc/ip16/include](../../ip16/include), searched second (its
`stddef.h` and `stdint.h` are AVR's, which MSP430's own replace); the target-neutral
ones are in [libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/msp430/include -I libc/ip16/include -I libc/common/include prog.c prog.i
```

`int`, `short` and pointers are 16 bits, `long` 32 and `long long` 64; `size_t` is
`unsigned int`, `ptrdiff_t` is `int`, `wchar_t` is `long`, and plain `char` is unsigned:
the types are msp430-elf-gcc's, which the backend's ABI follows.  Every
type wider than `char` has alignment 2.  `float` is IEEE binary32, `double` and `long
double` binary64, all in software.  A variadic call passes the last named argument
and the variable ones on the stack, each in a whole number of 2-byte words, a structure
by its address: `va_list` is a `char *`, as GCC's is.  `jmp_buf` holds R4-R10, SP and
the return address.  The types and constants agree with GCC's `-mcpu=msp430` headers,
and with clang's but for `wchar_t`, `wint_t`, `sig_atomic_t` and the fast 8-bit types
(`HeadersAgreeWithGcc`/`HeadersAgreeWithClang` in `backend/msp430/test/interop_tests.cpp`).
See [backend/msp430/Plan.md](../../../backend/msp430/Plan.md).
