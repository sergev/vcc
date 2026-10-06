% setjmp and longjmp: newlib's libc/sys/mmixware/setjmp.S, the MMIXware-ABI branch of
% its #ifdef, in lowercase and without the preprocessor.  The notice below is kept
% unchanged, as it requires.
%
% Copyright (C) 2001 Hans-Peter Nilsson
%
% Permission to use, copy, modify, and distribute this software is
% freely granted, provided that the above copyright notice, this notice
% and the following disclaimer are preserved with no changes.
%
% THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
% IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
% WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
% PURPOSE.
%
% jmp_buf[5]:
%   0:  fp
%   1:  rJ (return-address)
%   2:  sp
%   3:  rO *before* the setjmp call.
%   4:  temporary storage.  Reserved between setjmp and longjmp.
%
% $251, $252 and $255 are scratch here: none holds anything live across a call.

    .text

    .global setjmp
    .p2align 2
setjmp:
% Store fp, sp and return address.  Recycle the static-chain and
% structure-return registers as temporary register, since we need to keep
% the jmp_buf (parameter 1) and the return address across a "POP".
    set     $251, $0
    stou    $253, $251, 0
    get     $252, rJ
    stou    $252, $251, 8
    stou    $254, $251, 16
    setl    $0, 0

% Jump through hoops to get the value of rO *before* the setjmp call.
    geta    $255, 0f
    put     rJ, $255
    pop     1, 0
0:
    get     $255, rO
    stou    $255, $251, 24
    go      $255, $252, 0

    .global longjmp
    .p2align 2
longjmp:
% Reset arg2 to 1 if it is 0 (see longjmp(2)) and store it in jmp_buf.
% Save arg1 in a global register, since it will be destroyed by the POPs
% (in the mmixware ABI).
    csz     $1, $1, 1
    stou    $1, $0, 32
    set     $251, $0

% Loop and "POP 0,0" until rO is the expected value, like
% the expansion of nonlocal_goto_receiver, except that we put the return
% value in the right register and make sure that the POP causes it to
% enter the right return-value register as seen by the caller.
    geta    $255, 0f
    put     rJ, $255
    ldou    $255, $251, 24
0:
    get     $252, rO
    cmpu    $252, $252, $255
    bnp     $252, 1f
    ldou    $0, $251, 32
    pop     1, 0
1:
    ldou    $253, $251, 0
    ldou    $255, $251, 8
    ldou    $254, $251, 16
    go      $255, $255, 0
