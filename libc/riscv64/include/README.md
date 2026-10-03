# RISC-V standard library headers

The C11 headers that are RISC-V's own: `setjmp.h`, `stdarg.h`, `stddef.h`,
`stdint.h`.  The LP64 data-model headers it shares with AArch64 (`float.h`,
`inttypes.h`, `limits.h`, `math.h`) are in [libc/lp64/include](../../lp64/include),
searched second, and the target-neutral ones in
[libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/riscv64/include -I libc/lp64/include -I libc/common/include prog.c prog.i
```

`int` is 32 bits, `long` and pointers 64, plain `char` unsigned, `wchar_t` an `int`;
`float` and `double` are IEEE binary32 and binary64.  `<stdarg.h>` walks the a0–a7
save area a variadic function sets up below its stack arguments, 8 bytes per
argument; a struct wider than 16 bytes is passed as a pointer to a copy.
