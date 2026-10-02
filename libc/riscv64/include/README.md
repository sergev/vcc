# RISC-V standard library headers

The C11 headers that depend on the RISC-V LP64 data model: `float.h`, `inttypes.h`,
`limits.h`, `math.h`, `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h`.  The
target-neutral headers are in [libc/common/include](../../common/include), which is
searched second:

```sh
cc -E -nostdinc -I libc/riscv64/include -I libc/common/include prog.c prog.i
```

`int` is 32 bits, `long` and pointers 64, plain `char` unsigned; `float` and `double`
are IEEE binary32 and binary64.  `<stdarg.h>` walks the a0–a7 save area a variadic
function sets up below its stack arguments, 8 bytes per argument; a struct wider than
16 bytes is passed as a pointer to a copy.
