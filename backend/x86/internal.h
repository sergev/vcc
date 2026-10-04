//
// x86-64 code generator internals (System V psABI; see backend/x86/Plan.md).
//
// Registers: rdi, rsi, rdx, rcx, r8, r9 and xmm0-xmm7 carry arguments, rax/rdx and
// xmm0/xmm1 results; rax, r10 and r11 are the scratch registers of instruction
// selection, xmm14/xmm15 the SSE ones; rbx, rbp and r12-r15 are callee-saved, no xmm
// register is; rbp is the frame pointer, rsp the stack pointer.
//
#ifndef X86_INTERNAL_H
#define X86_INTERNAL_H

#include "tac.h"
#include "x86.h"

typedef struct {
    X86_Func *fn;
    const Tac_TopLevel *program; // the translation unit
    const Tac_TopLevel *tl;      // the function
} Gen;

const char *gen_name(const Gen *g);
X86_Instr *emit0(Gen *g, X86_Op op, X86_Width width);
X86_Instr *emit1(Gen *g, X86_Op op, X86_Width width, X86_Operand a);
X86_Instr *emit2(Gen *g, X86_Op op, X86_Width width, X86_Operand src, X86_Operand dst);
// reg = imm, at width (X86_L zero-extends to 64 bits).
void gen_li(Gen *g, int reg, X86_Width width, int64_t imm);

#endif // X86_INTERNAL_H
