# ARM32 standard library headers

The C11 headers that are ARM32's own: `float.h`, `setjmp.h`, `stdarg.h`, `stddef.h`,
`stdint.h`.  Those shared with the other ILP32 target (`inttypes.h`, `limits.h`,
`math.h`) are in [libc/ilp32/include](../../ilp32/include), searched second, and the
target-neutral ones in [libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/arm32/include -I libc/ilp32/include -I libc/common/include prog.c prog.i
```

`int`, `long` and pointers are 32 bits, `long long` 64, plain `char` unsigned and
`wchar_t` `unsigned int`; `float` and `double` are IEEE binary32 and binary64, and
`long double` is a `double`.  `<stdarg.h>` walks the area a variadic function makes by
pushing r0–r3 just below its stack arguments, 4 bytes per slot, a `double` or
`long long` at an 8-byte boundary; every argument is passed by value.  `va_list` is
clang's `struct __va_list`, so a `va_list` can be handed to clang-compiled code.
