# MSP430 standard library headers

The C11 headers that depend on MSP430's data model: `float.h`, `limits.h`, `math.h` and
`stdarg.h`.  `inttypes.h`, `stddef.h` and `stdint.h` describe the 16-bit data model
MSP430 shares with AVR, in [libc/ip16/include](../../ip16/include), searched second; the
target-neutral ones are in [libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/msp430/include -I libc/ip16/include -I libc/common/include prog.c prog.i
```

`int`, `short` and pointers are 16 bits, `long` 32 and `long long` 64; `size_t` is
`unsigned int`, `ptrdiff_t` and `wchar_t` are `int`, and plain `char` is unsigned.  Every
type wider than `char` has alignment 2.  `float` is IEEE binary32, `double` and `long
double` binary64, all in software.  A variadic function takes all its arguments on the
stack, in order, each in a whole number of 2-byte words: `va_list` is a `char *`, as
clang's for the target.  The backend is being written: see
[backend/msp430/Plan.md](../../../backend/msp430/Plan.md).
