; The MSP430 EABI helpers with two 64-bit operands, as clang calls them: the first
; operand in r8-r11 (r11 high), the second in r12-r15, the result in r12-r15 (an int in
; r12 for a comparison).  Each shim moves them to the ordinary ABI of the libgcc-named
; routine it calls -- the first operand in r12-r15, the second on the stack -- and
; preserves r4-r10, as an ordinary call does.
;
;   __mspabi_mpyll   __muldi3       __mspabi_divlli  __divdi3     __mspabi_remlli  __moddi3
;   __mspabi_divull  __udivdi3      __mspabi_remull  __umoddi3
;   __mspabi_addd    __adddf3       __mspabi_subd    __subdf3     __mspabi_mpyd    __muldf3
;   __mspabi_divd    __divdf3       __mspabi_cmpd    __ltdf2 (-1, 0 or 1; 1 when unordered)

    .macro  shim name, target
    .globl  \name
    .type   \name, @function
\name:
    sub     #8, r1
    mov     r12, 0(r1)
    mov     r13, 2(r1)
    mov     r14, 4(r1)
    mov     r15, 6(r1)
    mov     r8, r12
    mov     r9, r13
    mov     r10, r14
    mov     r11, r15
    call    #\target
    add     #8, r1
    ret
    .size   \name, .-\name
    .endm

    .text
    shim    __mspabi_mpyll, __muldi3
    shim    __mspabi_divlli, __divdi3
    shim    __mspabi_remlli, __moddi3
    shim    __mspabi_divull, __udivdi3
    shim    __mspabi_remull, __umoddi3
    shim    __mspabi_addd, __adddf3
    shim    __mspabi_subd, __subdf3
    shim    __mspabi_mpyd, __muldf3
    shim    __mspabi_divd, __divdf3
    shim    __mspabi_cmpd, __ltdf2
