//
// setjmp/longjmp for x86-64 (System V).  jmp_buf, 9 quadwords:
//   0 rbx, 8 rbp, 16 r12, 24 r13, 32 r14, 40 r15,
//   48 rsp as after setjmp returns, 56 the return address,
//   64 MXCSR (low 4 bytes) and the x87 control word (bytes 4-5).
//
    .text
    .globl  setjmp
    .type   setjmp, @function
setjmp:
    movq    %rbx, (%rdi)
    movq    %rbp, 8(%rdi)
    movq    %r12, 16(%rdi)
    movq    %r13, 24(%rdi)
    movq    %r14, 32(%rdi)
    movq    %r15, 40(%rdi)
    leaq    8(%rsp), %rax
    movq    %rax, 48(%rdi)
    movq    (%rsp), %rax
    movq    %rax, 56(%rdi)
    stmxcsr 64(%rdi)
    fnstcw  68(%rdi)
    xorl    %eax, %eax
    ret
    .size   setjmp, .-setjmp

// longjmp(env, val): setjmp returns val again, or 1 when val is 0.
    .globl  longjmp
    .type   longjmp, @function
longjmp:
    movl    %esi, %eax
    testl   %eax, %eax
    jnz     1f
    movl    $1, %eax
1:  ldmxcsr 64(%rdi)
    fldcw   68(%rdi)
    movq    (%rdi), %rbx
    movq    8(%rdi), %rbp
    movq    16(%rdi), %r12
    movq    24(%rdi), %r13
    movq    32(%rdi), %r14
    movq    40(%rdi), %r15
    movq    48(%rdi), %rsp
    jmpq    *56(%rdi)
    .size   longjmp, .-longjmp
