# RISC-V 32-bit standard library headers

The C11 headers that depend on the RISC-V ILP32 data model: `float.h`, `inttypes.h`,
`limits.h`, `math.h`, `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h`.  The
target-neutral headers are in [libc/common/include](../../common/include), which is
searched second:

```sh
cc -E -nostdinc -I libc/riscv32/include -I libc/common/include prog.c prog.i
```

`int`, `long` and pointers are 32 bits, `long long` 64, plain `char` unsigned; `float`
and `double` are IEEE binary32 and binary64, `long double` binary128.  `<stdarg.h>`
walks the a0–a7 save area a variadic function sets up below its stack arguments, 4
bytes per slot, a `double` or `long long` at an 8-byte boundary; a value wider than 8
bytes is passed as a pointer to a copy.
