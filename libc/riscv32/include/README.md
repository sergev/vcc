# RISC-V 32-bit standard library headers

The C11 headers that are RISC-V's own on ILP32: `float.h`, `setjmp.h`, `stdarg.h`,
`stddef.h`, `stdint.h`.  Those shared with the other ILP32 target (`inttypes.h`,
`limits.h`, `math.h`) are in [libc/ilp32/include](../../ilp32/include), searched
second, and the target-neutral ones in [libc/common/include](../../common/include),
searched last:

```sh
cc -E -nostdinc -I libc/riscv32/include -I libc/ilp32/include -I libc/common/include prog.c prog.i
```

`int`, `long` and pointers are 32 bits, `long long` 64, plain `char` unsigned; `float`
and `double` are IEEE binary32 and binary64, `long double` binary128.  `<stdarg.h>`
walks the a0–a7 save area a variadic function sets up below its stack arguments, 4
bytes per slot, a `double` or `long long` at an 8-byte boundary; a value wider than 8
bytes is passed as a pointer to a copy.
