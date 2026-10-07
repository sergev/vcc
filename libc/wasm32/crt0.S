// Startup for wasm32 (see docs/Wasm_Backend.md): _start, wasm-ld's default entry, runs
// the static constructors, calls main through __main_void and exits with its result.
// Built with PRINT_STATUS (crt0-status.o, for the book run tests) it prints the result
// as a signed decimal and a newline first.

	.functype	__wasm_call_ctors () -> ()
	.functype	__main_void () -> (i32)
	.functype	exit (i32) -> ()
	.functype	putbyte (i32) -> ()
#ifdef PRINT_STATUS
	.functype	__print_status (i32) -> ()
	.functype	__print_unsigned (i64) -> ()
#endif

	.section	.text._start,"",@
	.globl	_start
	.type	_start,@function
_start:
	.functype	_start () -> ()
	.local	i32
	call	__wasm_call_ctors
	call	__main_void
	local.set	0
#ifdef PRINT_STATUS
	local.get	0
	call	__print_status
#endif
	local.get	0
	call	exit
	end_function

#ifdef PRINT_STATUS
// void __print_status(int v): v in decimal, then a newline.
	.section	.text.__print_status,"",@
	.type	__print_status,@function
__print_status:
	.functype	__print_status (i32) -> ()
	local.get	0
	i32.const	0
	i32.lt_s
	if
	i32.const	45		// '-'
	call	putbyte
	i64.const	0
	local.get	0
	i64.extend_i32_s
	i64.sub
	call	__print_unsigned
	else
	local.get	0
	i64.extend_i32_u
	call	__print_unsigned
	end_if
	i32.const	10
	call	putbyte
	end_function

// void __print_unsigned(unsigned long long n): its digits, the leading ones first.
	.section	.text.__print_unsigned,"",@
	.type	__print_unsigned,@function
__print_unsigned:
	.functype	__print_unsigned (i64) -> ()
	local.get	0
	i64.const	10
	i64.ge_u
	if
	local.get	0
	i64.const	10
	i64.div_u
	call	__print_unsigned
	end_if
	local.get	0
	i64.const	10
	i64.rem_u
	i32.wrap_i64
	i32.const	48		// '0'
	i32.add
	call	putbyte
	end_function
#endif
