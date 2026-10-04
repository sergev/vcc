# AVR standard library headers

The C11 headers that depend on AVR's data model: `float.h`, `limits.h`, `math.h`,
`setjmp.h` and `stdarg.h`.  `inttypes.h`, `stddef.h` and `stdint.h` describe the 16-bit
data model AVR shares with MSP430, in [libc/ip16/include](../../ip16/include), searched
second; the target-neutral ones are in [libc/common/include](../../common/include),
searched last:

```sh
cc -E -nostdinc -I libc/avr/include -I libc/ip16/include -I libc/common/include prog.c prog.i
```

`int`, `short` and pointers are 16 bits, `long` 32 and `long long` 64; `size_t` is
`unsigned int`, `ptrdiff_t` and `wchar_t` are `int`, and plain `char` is signed.  Every
type has alignment 1.  `float`, `double` and `long double` are all IEEE binary32, so the
`FLT_*`, `DBL_*` and `LDBL_*` limits agree, and `math.h` makes each `double` function
(`fabs`, `ldexp`, …) serve as its `f` and `l` forms too.  A variadic function takes all
its arguments on the stack, in order and unaligned: `va_list` is a `char *`, as clang's
for the target.  `setjmp`/`longjmp` (`libc/avr/setjmp.s`) save r2–r17, Y, SP, SREG and
the return address in a 23-byte `jmp_buf`.  The `avr-headers` CTests check
that every header preprocesses and parses; the `HeadersAgreeWithClang` test of
`avr-tests` checks the limits and type sizes against clang's own headers.
