# Console and exit for wasm32: two functions the host supplies, env.putch, which
# writes one byte to standard output, and env.exit, which ends the program with a
# status (libc/wasm32/run.mjs is such a host).

	.functype	__vcc_putch (i32) -> ()
	.import_module	__vcc_putch, env
	.import_name	__vcc_putch, putch
	.functype	__vcc_exit (i32) -> ()
	.import_module	__vcc_exit, env
	.import_name	__vcc_exit, exit

# void putbyte(int b): write one byte.
	.section	.text.putbyte,"",@
	.globl	putbyte
	.type	putbyte,@function
putbyte:
	.functype	putbyte (i32) -> ()
	local.get	0
	call	__vcc_putch
	end_function

# void putch(unsigned c): putbyte.
	.section	.text.putch,"",@
	.globl	putch
	.type	putch,@function
putch:
	.functype	putch (i32) -> ()
	local.get	0
	call	__vcc_putch
	end_function

# void flush(void): the host buffers the output and writes it out at the end.
	.section	.text.flush,"",@
	.globl	flush
	.type	flush,@function
flush:
	.functype	flush () -> ()
	end_function

# _Noreturn void exit(int status): the host does not return.
	.section	.text.exit,"",@
	.globl	exit
	.type	exit,@function
exit:
	.functype	exit (i32) -> ()
	local.get	0
	call	__vcc_exit
	unreachable
	end_function
