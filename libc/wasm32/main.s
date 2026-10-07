# int __main_void(void): what crt0 calls.  A unit defining main(void) defines it too,
# as clang does; this weak one serves a main(argc, argv), named __main_argc_argv, and
# passes it no arguments.

	.functype	__main_argc_argv (i32, i32) -> (i32)

	.section	.text.__main_void,"",@
	.weak	__main_void
	.type	__main_void,@function
__main_void:
	.functype	__main_void () -> (i32)
	i32.const	0
	i32.const	0
	call	__main_argc_argv
	end_function
