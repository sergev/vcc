% Console and exit for Knuth's MMIX simulator mmix, through the MMIXware simulator calls
% (trap 0, <call>, <handle>, the parameter in $255, the result back in $255): stdout is
% buffered and written with Fwrite to StdOut, getch reads with Fread from StdIn (mmix -f
% feeds it), and exit reports "[exit N]" on StdErr before it halts.  The numbers are
% written out, since the assembler runs with -no-predefined-syms.
%
% Every function is entered by pushj with its arguments in $0..., and saves rJ in a local
% when it calls another.  divu divides rD:$Y, so these rely on rD = 0, as crt0 checks.

    .equ    Halt, 0
    .equ    Fread, 3
    .equ    Fputs, 7
    .equ    Fwrite, 6
    .equ    StdIn, 0
    .equ    StdOut, 1
    .equ    StdErr, 2
    .equ    OUTBUF_LOG2, 10     % a 1 KB output buffer

    .text

% void putbyte(int b): append one byte to the output buffer, which is written out when
% full, by flush and at exit.
    .global putbyte
    .p2align 2
putbyte:
    ldo     $1, __outcnt
    lda     $2, __outbuf
    stbu    $0, $2, $1
    addu    $1, $1, 1
    sto     $1, __outcnt
    sru     $2, $1, OUTBUF_LOG2
    bz      $2, 1f
    get     $3, rJ
    pushj   $4, flush
    put     rJ, $3
1:  pop     0, 0

% void putch(unsigned c): putbyte.
    .global putch
    .p2align 2
putch:
    jmp     putbyte

% void flush(void): write the buffered output to StdOut.
    .global flush
    .p2align 2
flush:
    ldo     $0, __outcnt
    bz      $0, 1f
    lda     $1, __outblk        % Fwrite's parameters: the address, the size
    lda     $2, __outbuf
    sto     $2, $1, 0
    sto     $0, $1, 8
    set     $255, $1
    trap    0, Fwrite, StdOut
    setl    $0, 0
    sto     $0, __outcnt
1:  pop     0, 0

% int getch(void): the next byte of StdIn, or -1 at its end.  Fread returns 0 when it
% read the whole request, and a negative number when it read less.
    .global getch
    .p2align 2
getch:
    lda     $0, __inblk
    lda     $1, __inbyte
    sto     $1, $0, 0
    setl    $1, 1
    sto     $1, $0, 8
    set     $255, $0
    trap    0, Fread, StdIn
    bnz     $255, 1f
    ldbu    $0, __inbyte
    pop     1, 0
1:  negu    $0, 0, 1
    pop     1, 0

% char *__mmix_fmtdec(long v, char *end): the decimal digits of v, with a '-' if it is
% negative, written to the bytes just below end; returns the first.  LONG_MIN negates
% to itself and reads as unsigned.
    .global __mmix_fmtdec
    .p2align 2
__mmix_fmtdec:
    set     $2, $0
    bnn     $0, 1f
    negu    $2, 0, $0
1:  divu    $2, $2, 10
    get     $3, rR
    addu    $3, $3, 48          % '0'
    subu    $1, $1, 1
    stbu    $3, $1, 0
    bnz     $2, 1b
    bnn     $0, 2f
    setl    $3, 45              % '-'
    subu    $1, $1, 1
    stbu    $3, $1, 0
2:  set     $0, $1
    pop     1, 0

% _Noreturn void exit(int status): flush the output, report "[exit N]" on StdErr and
% halt with the status in $255, which mmix makes its exit status.  The report tells a
% real exit from a jump into zeroed memory, which holds trap 0,0,0 too.
    .global exit
    .p2align 2
exit:
    pushj   $1, flush           % $0, the status, survives
    geta    $255, exit_msg
    trap    0, Fputs, StdErr
    lda     $3, __numbuf
    addu    $3, $3, 24          % the digits end here, then "]\n"
    setl    $1, 93              % ']'
    stbu    $1, $3, 0
    setl    $1, 10              % '\n'
    stbu    $1, $3, 1
    setl    $1, 0
    stbu    $1, $3, 2
    set     $2, $0
    pushj   $1, __mmix_fmtdec
    set     $255, $1
    trap    0, Fputs, StdErr
    set     $255, $0
    trap    0, Halt, 0

    .section .rodata
    .p2align 2                  % geta reaches only multiples of 4
exit_msg:
    .asciz  "[exit "

    .data
    .p2align 3
__outcnt:
    .quad   0
__outblk:
    .quad   0, 0
__inblk:
    .quad   0, 0
__numbuf:
    .zero   32
__inbyte:
    .byte   0

    .bss
__outbuf:
    .zero   1 << OUTBUF_LOG2
