; C startup for the ATmega1280 on bare-metal qemu (`-M arduino-mega -bios`): set the
; stack, copy .data (with .rodata in it) from flash to SRAM, clear .bss, enable the
; USART transmitters, call main and pass its result to exit.  Built with -DPRINT_STATUS
; it first prints main's result as "%d\n" (the book run tests compare it).
;
; Interrupts stay disabled throughout; every vector but reset goes to __bad_interrupt,
; which reports and stops.

    .equ    SREG, 0x3f
    .equ    __SP_H__, 0x3e
    .equ    __SP_L__, 0x3d
    .equ    RAMPZ, 0x3b
    .equ    UCSR0B, 0xc1
    .equ    UCSR1B, 0xc9
    .equ    TXEN, 3

; The vector table: reset, then the 56 interrupts, a 4-byte jmp each.
    .section .vectors, "ax", @progbits
    .globl  _start
_start:
    jmp     __reset
    .rept   56
    jmp     __bad_interrupt
    .endr

    .text
__reset:
    clr     r1                          ; __zero_reg__
    out     SREG, r1
    ldi     r28, lo8(__stack_top)
    ldi     r29, hi8(__stack_top)
    out     __SP_H__, r29
    out     __SP_L__, r28

; Copy .data from its load address in flash, which may lie above 64 KB.  clang's
; objects reference these two names (avr-libc's), so they are defined here.
    .globl  __do_copy_data
__do_copy_data:
    ldi     r17, hi8(__data_end)
    ldi     r26, lo8(__data_start)
    ldi     r27, hi8(__data_start)
    ldi     r30, lo8(__data_load_start)
    ldi     r31, hi8(__data_load_start)
    ldi     r16, hh8(__data_load_start)
    out     RAMPZ, r16
    rjmp    2f
1:  elpm    r0, Z+
    st      X+, r0
2:  cpi     r26, lo8(__data_end)
    cpc     r27, r17
    brne    1b

    .globl  __do_clear_bss
__do_clear_bss:
    ldi     r17, hi8(__bss_end)
    ldi     r26, lo8(__bss_start)
    ldi     r27, hi8(__bss_start)
    rjmp    2f
1:  st      X+, r1
2:  cpi     r26, lo8(__bss_end)
    cpc     r27, r17
    brne    1b

; The canary between .bss and the heap: exit reports a stack that ran into it.
    ldi     r24, 0xa5
    sts     __stack_canary, r24
    ldi     r24, 0x5a
    sts     __stack_canary + 1, r24

; USART0 is stdout, USART1 carries main's result to the run harness.
    ldi     r24, 1 << TXEN
    sts     UCSR0B, r24
    sts     UCSR1B, r24

    call    main
    movw    r16, r24                    ; the status, for exit
#ifdef PRINT_STATUS
    movw    r28, r24
    sbrs    r29, 7
    rjmp    2f
    ldi     r24, '-'
    call    putbyte
    com     r29                         ; -32768 stays itself, read as unsigned below
    neg     r28
    sbci    r29, 0xff
2:  clr     r15                         ; digits pushed, least significant first
3:  movw    r24, r28
    ldi     r22, 10
    ldi     r23, 0
    call    __udivmodhi4                ; quotient r23:r22, remainder r25:r24
    subi    r24, -'0'
    push    r24
    inc     r15
    movw    r28, r22
    sbiw    r28, 0
    brne    3b
4:  pop     r24
    call    putbyte
    dec     r15
    brne    4b
    ldi     r24, '\n'
    call    putbyte
#endif
    movw    r24, r16
    jmp     exit

; An interrupt, which nothing enables: say so, and stop with status 0xfe.
__bad_interrupt:
    ldi     r30, lo8(bad_msg)
    ldi     r31, hi8(bad_msg)
1:  ld      r24, Z+
    tst     r24
    breq    2f
    call    putbyte
    rjmp    1b
2:  ldi     r24, 0xfe
    jmp     exit

    .section .rodata
bad_msg:
    .asciz  "unexpected interrupt\n"

; The canary, outside .bss so that clearing .bss does not touch it.
    .section .noinit, "aw", @nobits
    .globl  __stack_canary
__stack_canary:
    .zero   2
