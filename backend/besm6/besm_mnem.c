#include "besm.h"
#include "internal.h"

// Latin machine-instruction mnemonics indexed by Besm_InstrKind.  Shared by the Madlen
// and Unix (b6as) emitters, which use identical mnemonics and differ only in line
// framing.  Only the machine instructions have an entry; assembler directives and data
// pseudo-ops (BESM_STMT_*, BESM_DATA_*) are BESM_SHAPE_SPECIAL and their per-dialect
// spelling lives in the emitter, so they stay NULL here.
const char *const besm_latin_mnem[] = {
    [BESM_MEM_XTA] = "xta",
    [BESM_MEM_ATX] = "atx",
    [BESM_MEM_STX] = "stx",
    [BESM_MEM_XTS] = "xts",
    [BESM_MEM_ITA] = "ita",
    [BESM_MEM_ATI] = "ati",
    [BESM_MEM_ITS] = "its",
    [BESM_MEM_STI] = "sti",
    [BESM_MEM_MTJ] = "mtj",

    [BESM_ARITH_ADD]    = "a+x",
    [BESM_ARITH_SUB]    = "a-x",
    [BESM_ARITH_RSUB]   = "x-a",
    [BESM_ARITH_ABSSUB] = "amx",
    [BESM_ARITH_MUL]    = "a*x",
    [BESM_ARITH_DIV]    = "a/x",
    [BESM_ARITH_CNEG]   = "avx",

    [BESM_LOG_AAX] = "aax",
    [BESM_LOG_AOX] = "aox",
    [BESM_LOG_AEX] = "aex",
    [BESM_LOG_ARX] = "arx",
    [BESM_LOG_APX] = "apx",
    [BESM_LOG_AUX] = "aux",
    [BESM_LOG_ACX] = "acx",
    [BESM_LOG_ANX] = "anx",

    [BESM_EXP_EADDX]   = "e+x",
    [BESM_EXP_ESUBX]   = "e-x",
    [BESM_EXP_SHIFTX]  = "asx",
    [BESM_EXP_SETRMEM] = "xtr",
    [BESM_EXP_GETR]    = "rte",
    [BESM_EXP_YTA]     = "yta",
    [BESM_EXP_EADDN]   = "e+n",
    [BESM_EXP_ESUBN]   = "e-n",
    [BESM_EXP_SHIFTN]  = "asn",
    [BESM_EXP_SETR]    = "ntr",

    [BESM_REG_VTM]   = "vtm",
    [BESM_REG_UTM]   = "utm",
    [BESM_REG_JADDM] = "j+m",

    [BESM_MOD_UTC] = "utc",
    [BESM_MOD_WTC] = "wtc",

    [BESM_BRANCH_UZA]  = "uza",
    [BESM_BRANCH_U1A]  = "u1a",
    [BESM_BRANCH_UJ]   = "uj",
    [BESM_BRANCH_VJM]  = "vjm",
    [BESM_BRANCH_VZM]  = "vzm",
    [BESM_BRANCH_V1M]  = "v1m",
    [BESM_BRANCH_VLM]  = "vlm",
    [BESM_BRANCH_STOP] = "stop",

    [BESM_IO_EXT] = "ext",
    [BESM_IO_MOD] = "mod",

    // Directives / data / UTM / CALL / BASE are BESM_SHAPE_SPECIAL — no shared entry.
    [BESM_DATA_Z00] = NULL,
};

// Classify how an instruction's operand and modifier-register field are formed.  The
// machine instructions fall into three regular shapes; everything with dialect-specific
// framing (directives, data pseudo-ops, and the few irregular machine ops UTM/CALL/BASE)
// is BESM_SHAPE_SPECIAL and handled explicitly by each emitter.
Besm_OperandShape besm_operand_shape(Besm_InstrKind kind)
{
    switch (kind) {
    // Memory/const operand via the dialect's operand formatter; mreg = instr->reg.
    case BESM_MEM_XTA:
    case BESM_MEM_ATX:
    case BESM_MEM_STX:
    case BESM_MEM_XTS:
    case BESM_ARITH_ADD:
    case BESM_ARITH_SUB:
    case BESM_ARITH_RSUB:
    case BESM_ARITH_ABSSUB:
    case BESM_ARITH_MUL:
    case BESM_ARITH_DIV:
    case BESM_ARITH_CNEG:
    case BESM_LOG_AAX:
    case BESM_LOG_AOX:
    case BESM_LOG_AEX:
    case BESM_LOG_ARX:
    case BESM_LOG_APX:
    case BESM_LOG_AUX:
    case BESM_LOG_ACX:
    case BESM_LOG_ANX:
    case BESM_EXP_EADDX:
    case BESM_EXP_ESUBX:
    case BESM_EXP_SHIFTX:
    case BESM_EXP_SETRMEM:
    case BESM_MOD_UTC:
    case BESM_MOD_WTC:
    case BESM_BRANCH_UZA:
    case BESM_BRANCH_U1A:
    case BESM_BRANCH_UJ:
    case BESM_BRANCH_VJM:
    case BESM_BRANCH_VZM:
    case BESM_BRANCH_V1M:
    case BESM_BRANCH_VLM:
    // EXT/MOD are Format-1 like every kind above, so the same shape renders them: a constant
    // device address is the instruction's own 12-bit offset field (`,ext, 2073`), a computed
    // one rides in the modifier register (`14 ,ext,`, EA = M[14] + 0).  Their operand is not
    // a memory word — EA *is* the register being addressed.
    case BESM_IO_EXT:
    case BESM_IO_MOD:
        return BESM_SHAPE_MEM;

    // Immediate decimal operand; no modifier register.  STOP is here for its halt code, which
    // rides in its own 15-bit Format-2 address field (`stop 5` / `стоп 5`; Madlen names no halt
    // at all and writes the raw octal opcode instead — see mad_mnem in emit_madlen.c).
    case BESM_MEM_ITA:
    case BESM_MEM_ATI:
    case BESM_MEM_ITS:
    case BESM_MEM_STI:
    case BESM_EXP_GETR:
    case BESM_EXP_YTA:
    case BESM_EXP_EADDN:
    case BESM_EXP_ESUBN:
    case BESM_EXP_SHIFTN:
    case BESM_EXP_SETR:
    case BESM_BRANCH_STOP:
        return BESM_SHAPE_IMM0;

    // Immediate decimal operand; mreg = instr->reg.
    case BESM_MEM_MTJ:
    case BESM_REG_VTM:
    case BESM_REG_JADDM:
        return BESM_SHAPE_IMMR;

    // UTM (operand suppressed when zero), CALL/BASE (name operand), the extracode (whose
    // mnemonic *is* its opcode, spelled `,*71,` / `э71` / `$77` — hence no shared entry in
    // either mnemonic table), the assembler directives, and every data pseudo-op need
    // dialect-specific formatting.
    default:
        return BESM_SHAPE_SPECIAL;
    }
}
