# x86-64 standard library headers

The C11 headers that are x86-64's own: `float.h`, `limits.h`, `setjmp.h`, `stdarg.h`,
`stddef.h`, `stdint.h`.  Those shared with the other LP64 targets (`inttypes.h`,
`math.h`) are in [libc/lp64/include](../../lp64/include), searched second, and the
target-neutral ones in [libc/common/include](../../common/include), searched last:

```sh
cc -E -nostdinc -I libc/x86/include -I libc/lp64/include -I libc/common/include prog.c prog.i
```

`int` is 32 bits, `long` and pointers 64, plain `char` signed and `wchar_t` an `int`;
`float` and `double` are IEEE binary32 and binary64, and `long double` is the x87
80-bit extended format in a 16-byte slot.  `limits.h` is our own rather than the LP64
one for the signed `char`, `float.h` for the x87 `long double`.  `va_list` is clang's
array of one `__va_list_tag`, so it passes as a pointer and can be handed to
clang-compiled code; `<stdarg.h>` steps through the register save area a variadic
function fills from `rdi`–`r9` and `xmm0`–`xmm7`, then the stack arguments.
`setjmp`/`longjmp` (`libc/x86/setjmp.s`) save the callee-saved registers, the stack
pointer, the return address and the MXCSR and x87 control words.
