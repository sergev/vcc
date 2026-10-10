#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "internal.h"

//
// Bemsh (Cyrillic autocode) assembly emitter.
//
// Bemsh is the second Dubna-monitor dialect, so this emitter is structurally a port of
// emit_madlen.c: the same IR traversal and the same five-shape operand classifier
// (besm_operand_shape), differing only in spelling.  The three Bemsh-specific choices are:
//
//   * Cyrillic mnemonics/directives (besm_cyr_mnem[] below), emitted as readable lowercase
//     UTF-8 — Bemsh is case-insensitive, so lowercase is fine and keeps golden files
//     legible.  The mnemonic/directive text is NOT run through utf8_to_koi7; only program
//     string-literal *data* is KOI-7-encoded, and that happens upstream in static.c.
//   * The index register is written parenthesized *after* the address (`уиа масс(13)`),
//     not as a separate field before the mnemonic as in Madlen.
//   * Bemsh column form: the label begins in column 1 (blank column 1 = no label);
//     fields are space-separated.
//
// Name mangling is bemsh_mangle (below): ≤6-char, letter-first, deterministic, with the
// runtime-helper names mapped to the Bemsh libc (`libbem.bin`) exports — task B2.  Dubna
// run-integration is task B3.  Several SPECIAL renderings (call, внешн binding, финиш
// entry operand, Z00 displacement) are provisional golden-file forms, refined under B3.
//

// Cyrillic machine-instruction mnemonics indexed by Besm_InstrKind — the Bemsh counterpart
// of besm_latin_mnem[].  NULL for BESM_SHAPE_SPECIAL kinds (directives / data / UTM / CALL /
// BASE), whose Bemsh spelling lives in emit_bemsh_special.
static const char *const besm_cyr_mnem[] = {
    [BESM_MEM_XTA] = "сч",     [BESM_MEM_ATX] = "зп",     [BESM_MEM_STX] = "зпм",
    [BESM_MEM_XTS] = "счм",    [BESM_MEM_ITA] = "счи",    [BESM_MEM_ATI] = "уи",
    [BESM_MEM_ITS] = "счим",   [BESM_MEM_STI] = "уим",    [BESM_MEM_MTJ] = "уии",

    [BESM_ARITH_ADD] = "сл",   [BESM_ARITH_SUB] = "вч",   [BESM_ARITH_RSUB] = "вчоб",
    [BESM_ARITH_ABSSUB] = "вчаб", [BESM_ARITH_MUL] = "умн", [BESM_ARITH_DIV] = "дел",
    [BESM_ARITH_CNEG] = "знак",

    [BESM_LOG_AAX] = "и",      [BESM_LOG_AOX] = "или",    [BESM_LOG_AEX] = "нтж",
    [BESM_LOG_ARX] = "слц",    [BESM_LOG_APX] = "сбр",    [BESM_LOG_AUX] = "рзб",
    [BESM_LOG_ACX] = "чед",    [BESM_LOG_ANX] = "нед",

    [BESM_EXP_EADDX] = "слп",  [BESM_EXP_ESUBX] = "вчп",  [BESM_EXP_SHIFTX] = "сд",
    [BESM_EXP_SETRMEM] = "рж", [BESM_EXP_GETR] = "счрж",  [BESM_EXP_YTA] = "счмр",
    [BESM_EXP_EADDN] = "слпа", [BESM_EXP_ESUBN] = "вчпа", [BESM_EXP_SHIFTN] = "сда",
    [BESM_EXP_SETR] = "ржа",

    [BESM_REG_VTM] = "уиа",    [BESM_REG_UTM] = "слиа",   [BESM_REG_JADDM] = "сли",

    [BESM_MOD_UTC] = "мода",   [BESM_MOD_WTC] = "мод",

    [BESM_BRANCH_UZA] = "по",  [BESM_BRANCH_U1A] = "пе",  [BESM_BRANCH_UJ] = "пб",
    [BESM_BRANCH_VJM] = "пв",  [BESM_BRANCH_VZM] = "пио", [BESM_BRANCH_V1M] = "пино",
    [BESM_BRANCH_VLM] = "цикл", [BESM_BRANCH_STOP] = "стоп",

    [BESM_IO_EXT] = "увв",     [BESM_IO_MOD] = "рег",

    // Directives / data / UTM / CALL / BASE are BESM_SHAPE_SPECIAL — no shared entry.
    [BESM_DATA_Z00] = NULL,
};

// Format a double as the decimal-number field of a Bemsh Е (floating-point) constant.
// Like Madlen, Bemsh requires a mandatory decimal point ("2." not "2").  Copied from
// emit_madlen.c's mad_format_real so the two emitters stay decoupled.
static void bemsh_format_real(char *buf, size_t n, double val)
{
    // 13 significant digits round-trip a BESM-6 40-bit mantissa exactly.
    snprintf(buf, n, "%.13g", val);
    char *e = strpbrk(buf, "eE");
    if (!strchr(buf, '.') && !e) {
        double a = val < 0 ? -val : val;
        if (a >= 1099511627776.0) { // 2^40 — bare integer mantissa overflows the field
            snprintf(buf, n, "%.12e", val);
            e = strpbrk(buf, "eE");
        }
    }
    if (strchr(buf, '.'))
        return; // already has a decimal point
    size_t len = strlen(buf);
    if (e) {
        size_t pos = (size_t)(e - buf);
        if (len + 1 < n) {
            memmove(e + 1, e, len - pos + 1); // shift exponent (incl NUL) right by one
            *e = '.';
        }
    } else if (len + 1 < n) {
        buf[len]     = '.';
        buf[len + 1] = '\0';
    }
}

// Encode a C double as a native BESM-6 48-bit floating-point word (see
// backend/besm6/Besm6_Data_Representation.md §6): bits 48-42 = 7-bit exponent biased by 64, bit 41 =
// sign, bits 40-1 = 40-bit two's-complement mantissa.  Copied from emit_unix.c's
// unix_real_word so the two emitters stay decoupled; used only for the octal fallback below.
static uint64_t bemsh_real_word(double v)
{
    if (v == 0.0)
        return 0; // machine zero is the all-zero word

    int e2;
    double f  = frexp(v, &e2); // v = f * 2^e2, with 0.5 <= |f| < 1
    int64_t T = llround(ldexp(f, 40));
    int E     = e2 + 64;
    if (T >= (INT64_C(1) << 40)) { // f rounded up to 1.0: renormalize
        T >>= 1;
        E++;
    }
    if (T == -(INT64_C(1) << 39)) { // -0.5 is un-normalized (bit 41 == bit 40): use -1.0
        T = -(INT64_C(1) << 40);
        E--;
    }
    if (E < 1 || E > 127)
        fatal_error("floating constant %g is outside the BESM-6 exponent range", v);

    uint64_t mant = (uint64_t)T & ((UINT64_C(1) << 41) - 1); // bits 41-1
    return ((uint64_t)E << 41) | mant;
}

// True when the decimal mantissa digit-string of `num` fits Bemsh's type-Е field: its value
// (digits only, ignoring sign/point) must not exceed 2^40 - 1 = 1 099 511 627 775 (Bemsh
// §9.3).  2^40 itself (= 1.099511627776e12) has a 13-digit mantissa one over the limit and has
// no exact shorter-mantissa decimal form, so such constants take the octal fallback instead.
static bool bemsh_mantissa_fits(const char *num)
{
    uint64_t m = 0;
    for (const char *p = num; *p && *p != 'e' && *p != 'E'; p++) {
        if (*p >= '0' && *p <= '9') {
            m = m * 10 + (uint64_t)(*p - '0');
            if (m > UINT64_C(1099511627775))
                return false;
        }
    }
    return true;
}

// Build the operand body for a floating-point constant `val`, WITHOUT the leading `=` (a
// literal command adds it; a `конд` datum does not): normally the readable type-Е decimal
// literal `е'<decimal>'`, but for a value whose exact decimal mantissa overflows the Е field
// the exact octal bit pattern `в'<octal>'` (type В), which Bemsh always accepts.
static void bemsh_real_body(char *buf, size_t n, double val)
{
    char num[48];
    bemsh_format_real(num, sizeof(num), val);
    if (bemsh_mantissa_fits(num))
        snprintf(buf, n, "е'%s'", num);
    else
        snprintf(buf, n, "в'%" PRIo64 "'", bemsh_real_word(val));
}

// Runtime-helper name map.  The canonical IR carries runtime helpers with a `$` separator
// (`b$ret`, `b$save`); task B4's Bemsh runtime library `libbem.bin` exports each under the
// name `_NAME` (drop the leading `b`, `$`→`_`, ≤6 chars).  An exact-match table both
// guarantees the emitted symbol matches the library export and keeps a block-scope static
// suffixed `name$N` off the helper path (e.g. a static named `b` becomes `b$0`, which is not
// a helper and must take the general rule → `b0`).  The non-`b$` libc leaves `exit`/`frexp`/
// `ldexp` carry no `$`, are already ≤6 chars, and pass through the general rule unchanged.
// Kept in sync with the "Exported helper-symbol map" in libc/besm6/bemsh/README.md.
static const struct {
    const char *ir;    // canonical b$… name in the IR
    const char *bemsh; // libbem.bin export symbol
} bemsh_helper_map[] = {
    { "b$save", "_save" },   { "b$save0", "_save0" }, { "b$ret", "_ret" },
    { "b$mul", "_mul" },     { "b$div", "_div" },     { "b$mod", "_mod" },
    { "b$uadd", "_uadd" },   { "b$usub", "_usub" },   { "b$umul", "_umul" },
    { "b$udiv", "_udiv" },   { "b$umod", "_umod" },   { "b$uneg", "_uneg" },
    { "b$lsh", "_lsh" },     { "b$rsh", "_rsh" },     { "b$eq", "_eq" },
    { "b$ne", "_ne" },       { "b$lt", "_lt" },       { "b$le", "_le" },
    { "b$gt", "_gt" },       { "b$ge", "_ge" },       { "b$not", "_not" },
    { "b$ult", "_ult" },     { "b$ule", "_ule" },     { "b$ugt", "_ugt" },
    { "b$uge", "_uge" },     { "b$flt", "_flt" },     { "b$fle", "_fle" },
    { "b$fgt", "_fgt" },     { "b$fge", "_fge" },     { "b$dtoi", "_dtoi" },
    { "b$dtou", "_dtou" },   { "b$utod", "_utod" },   { "b$padd", "_padd" },
    { "b$pinc", "_pinc" },   { "b$pdec", "_pdec" },   { "b$pdiff", "_pdiff" },
    { "b$stb", "_stb" },     { "b$tout", "_tout" },
};

// Mangle a name into a valid Bemsh label: ≤6 chars, begins with a letter (a leading `_`
// counts — Dubna renders it as the Cyrillic letter Ю), letters/digits/`_` only.  This is a
// PURE deterministic function of `src` with no state, so a linkage label (global, function,
// string constant) mangles identically in every module and in every separately-compiled
// translation unit — the only scheme that keeps cross-TU linkage correct.  Order:
//   1. A `=…` literal-command operand passes through verbatim.
//   2. A runtime helper maps to its libbem.bin export (bemsh_helper_map).
//   3. General: keep [A-Za-z0-9] and `_`; drop `$`/`%`/`/` and any other char; prefix `T`
//      when the result would start with a digit; truncate to 6.
// Truncation can in principle map two long names to the same 6 chars; that residual collision
// risk is an accepted provisional B2 limitation, guarded by the mangler corpus unit test.
void bemsh_mangle(char *dst, size_t n, const char *src)
{
    if (n == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (src[0] == '=') { // literal-command operand — not an identifier
        snprintf(dst, n, "%s", src);
        return;
    }
    for (size_t k = 0; k < sizeof bemsh_helper_map / sizeof bemsh_helper_map[0]; k++) {
        if (strcmp(src, bemsh_helper_map[k].ir) == 0) {
            snprintf(dst, n, "%s", bemsh_helper_map[k].bemsh);
            return;
        }
    }

    size_t lim = n - 1 < 6 ? n - 1 : 6;
    size_t i   = 0;
    for (const char *p = src; *p && i < lim; p++) {
        char c    = *p;
        int alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (alnum) {
            if (i == 0 && c >= '0' && c <= '9') {
                dst[i++] = 'T'; // a label must begin with a letter
                if (i >= lim)
                    break;
            }
            dst[i++] = c;
        } else if (c == '_') {
            dst[i++] = '_'; // renders as the Cyrillic letter Ю (valid, letter-first-safe)
        }
        // '$', '%', '/', and any other character are dropped
    }
    if (i == 0)
        dst[i++] = 'T'; // nothing survived — still emit a valid bare label
    dst[i] = '\0';
}

//
// Convert a literal-command operand carried in Besm_Instr.name into its Bemsh spelling.
// The shared instruction selector (backend/besm6/instr.c) writes octal LOG-literal masks
// and immediates in *Madlen* syntax — `= <octal>` (right-justified, value as-is) and, in
// principle, `=: <octal>` (OCT, left-filled to a full word).  Bemsh writes both as a type-В
// (octal) literal command `=в'<octal>'` (the right-fill form) — a bare `=377` is rejected by
// Macro-Bemsh as a mistyped constant (`TИП KOHCT`, severity 4), which empties the library.
// A literal not in this octal form (already `=в'…'`/`=е'…'`, or anything else) passes through
// unchanged; the shared selector emits none such today, so this is a safety fallback.
static void bemsh_madlen_literal(char *buf, size_t n, const char *lit)
{
    const char *p = lit + 1; // skip '='
    int leftjust = 0;
    if (*p == ':') { // Madlen =: — OCT: the digits are left-justified in the 48-bit word
        leftjust = 1;
        p++;
    }
    if (*p == '\0') {
        snprintf(buf, n, "%s", lit);
        return;
    }
    for (const char *q = p; *q; q++) {
        if (*q < '0' || *q > '7') { // not a pure-octal literal — pass through verbatim
            snprintf(buf, n, "%s", lit);
            return;
        }
    }
    if (leftjust) {
        // OCT (`=:64` → `=в'6400000000000000'`): value at the left, zero-padded to 16 on the
        // right (a full word), vs the LOG default (`=64` → `=в'64'`, right-justified value).
        char padded[17];
        size_t len = strlen(p);
        if (len > 16)
            len = 16; // digits already fill the word
        memcpy(padded, p, len);
        memset(padded + len, '0', 16 - len);
        padded[16] = '\0';
        snprintf(buf, n, "=в'%s'", padded);
    } else {
        snprintf(buf, n, "=в'%s'", p);
    }
}

//
// Build the address-field string from (name, addr): "name+N" / "name-N" / "name" / "N".
// Names beginning with '=' are Bemsh literal-command operands (rendered by
// bemsh_madlen_literal); a literal never carries an address offset.
//
static void addr_str(char *buf, size_t n, const char *name, int addr)
{
    char sname[8];
    const char *aname = name;
    if (name && name[0] == '=') {
        bemsh_madlen_literal(buf, n, name);
        return;
    }
    if (name) {
        bemsh_mangle(sname, sizeof(sname), name);
        aname = sname;
    }
    if (name && addr > 0)
        snprintf(buf, n, "%s+%d", aname, addr);
    else if (name && addr < 0)
        snprintf(buf, n, "%s-%d", aname, -addr);
    else if (name)
        snprintf(buf, n, "%s", aname);
    else if (addr)
        snprintf(buf, n, "%d", addr);
}

//
// Format instruction `i`'s address operand.  A structural constant (`i->konst`) becomes a
// Bemsh literal command: `=в'<octal>'` for an integer (type В = octal; besm_const_word
// already yields the 48-bit word as an octal pattern), `=е'<value>'` for a real (type Е).
//
static void bemsh_operand(char *buf, size_t n, const Besm_Instr *i)
{
    if (i->konst) {
        Besm_ConstWord w = besm_const_word(i->konst);
        if (w.is_real) {
            char body[48];
            bemsh_real_body(body, sizeof(body), w.real_val);
            snprintf(buf, n, "=%s", body);
        } else {
            snprintf(buf, n, "=в'%" PRIo64 "'", w.word);
        }
        return;
    }
    addr_str(buf, n, i->name, i->addr);
}

//
// Emit one Bemsh statement line: label in column 1 (6-wide, blank if none), then the
// mnemonic, then the operand.  The index register (mreg) is appended parenthesized to the
// operand — `уиа масс(13)`, or `(13)` when there is no address.
//
static void emit_line(FILE *out, const char *label, int mreg, const char *mnem,
                      const char *addr)
{
    char operand[80] = "";
    if (addr && addr[0])
        snprintf(operand, sizeof(operand), "%s", addr);
    if (mreg) {
        size_t len = strlen(operand);
        snprintf(operand + len, sizeof(operand) - len, "(%d)", mreg);
    }

    if (label) {
        char sl[8];
        bemsh_mangle(sl, sizeof(sl), label);
        fprintf(out, "%-6s", sl);
    } else {
        fprintf(out, "      ");
    }
    fprintf(out, " %s", mnem);
    if (operand[0])
        fprintf(out, " %s", operand);
    fputc('\n', out);
}

//
// Emit a BESM_SHAPE_SPECIAL instruction — the kinds whose Bemsh spelling is not a plain
// machine mnemonic + regular operand: UTM (operand suppressed when zero), CALL/BASE, the
// assembler directives, and the data pseudo-ops.
//
static void emit_bemsh_special(FILE *out, const Besm_Instr *instr)
{
    char a[80] = "";
    switch (instr->kind) {
    // Index-register add (stack extension): bare `слиа` when the delta is zero.
    case BESM_REG_UTM:
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, NULL, instr->reg, "слиа", a);
        break;

    // Extracode: Bemsh names it `эNN`, the opcode in octal (`э71` is the print-line
    // extracode, as in libc/besm6/bemsh/b_tout.bemsh).  The effective address is the
    // ordinary (reg, addr) pair — emit_line appends the modifier register parenthesized.
    case BESM_IO_EXTRACODE: {
        char mnem[8];
        snprintf(mnem, sizeof(mnem), "э%o", instr->opcode);
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, NULL, instr->reg, mnem, a);
        break;
    }

    // Call: Bemsh has no auto-declaring `call` macro (Madlen's expands to `13 vjm name`),
    // so the call is rendered explicitly as `пв name(13)` — VJM through r13, which passes the
    // return address the callee restores via `пб (13)`.  Its `внешн` declaration is emitted
    // separately — codegen.c splices one BESM_STMT_SUBP per distinct call target after the
    // `,name,` for the Bemsh dialect.
    case BESM_BRANCH_CALL:
        bemsh_mangle(a, sizeof(a), instr->name);
        emit_line(out, NULL, REG_RET, "пв", a);
        break;
    case BESM_STMT_BASE:
        bemsh_mangle(a, sizeof(a), instr->name);
        emit_line(out, NULL, instr->reg, "употр", a);
        break;

    // Assembly directives — the defined name goes in the label field (column 1).
    case BESM_STMT_LABEL:
        // Define a code label as a labeled `ноп` (no-op).  `экв *` is wrong here: `*` is the
        // integer part of the half-cell address counter, so a label after an odd number of
        // instructions captures the current (right-half) cell while the next real instruction —
        // placed in the left half of a *fresh* cell (Bemsh §7.7) — lands one cell later, and a
        // branch to the `экв` label then hits the previous cell's left instruction.  A labeled
        // `ноп` is itself placed in the left half of a fresh cell, so the label names a
        // reachable instruction and control falls through it to the code that follows.
        emit_line(out, instr->name, 0, "ноп", "");
        break;
    case BESM_STMT_NAME: // open a named subprogram (auto entry point)
        // старт requires a start-address operand — the translator rejects an empty one.
        // The value is irrelevant for a relocatable module (the loader relocates it), so
        // use `1`, matching the hand-written libbem helpers (`_save старт 1`) and bemsh.dub.
        emit_line(out, instr->name, 0, "старт", "1");
        break;
    case BESM_STMT_SUBP: { // external reference — provisional `.label` (search-all) binding
        char ext[8];
        bemsh_mangle(ext, sizeof(ext), instr->name);
        snprintf(a, sizeof(a), ".%s", ext);
        emit_line(out, instr->name, 0, "внешн", a);
        break;
    }
    case BESM_STMT_ENTRY: // declare an additional entry point
        bemsh_mangle(a, sizeof(a), instr->name);
        emit_line(out, NULL, 0, "входн", a);
        break;
    case BESM_STMT_END:
        emit_line(out, NULL, 0, "финиш", "");
        break;

    // Data section directives.
    case BESM_DATA_LOG:
        snprintf(a, sizeof(a), "в'%" PRIo64 "'", instr->log_val);
        emit_line(out, instr->name, 0, "конд", a);
        break;
    case BESM_DATA_BSS:
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, instr->name, 0, "пам", a);
        break;
    case BESM_DATA_INT:
        snprintf(a, sizeof(a), "ф'%d'", instr->addr);
        emit_line(out, instr->name, 0, "конд", a);
        break;
    case BESM_DATA_REAL:
        bemsh_real_body(a, sizeof(a), instr->real_val);
        emit_line(out, instr->name, 0, "конд", a);
        break;
    case BESM_DATA_EQU:
        snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, NULL, 0, "экв", a);
        break;
    case BESM_DATA_REF: {
        char ref[8];
        bemsh_mangle(ref, sizeof(ref), instr->name);
        snprintf(a, sizeof(a), "а(%s)", ref);
        emit_line(out, NULL, 0, "конд", a);
        break;
    }
    case BESM_DATA_STRING: {
        const char *s = instr->name;
        while (*s) {
            snprintf(a, sizeof(a), "ф'%d'", (unsigned char)*s++);
            emit_line(out, NULL, 0, "конд", a);
        }
        emit_line(out, NULL, 0, "конд", "ф'0'");
        break;
    }
    case BESM_DATA_Z00: { // provisional address-constant word (fat-pointer fidelity: B2/B3)
        char body[64] = "";
        addr_str(body, sizeof(body), instr->name, instr->addr);
        snprintf(a, sizeof(a), "а(%s)", body);
        emit_line(out, instr->label, 0, "конд", a);
        break;
    }

    default:
        internal_error("besm6: instruction kind %d has no Bemsh form", (int)instr->kind);
    }
}

static void emit_bemsh_instr(FILE *out, const Besm_Instr *instr)
{
    for (; instr; instr = instr->next) {
        char a[80]       = "";
        Besm_InstrKind k = instr->kind;
        switch (besm_operand_shape(k)) {
        case BESM_SHAPE_MEM:
            bemsh_operand(a, sizeof(a), instr);
            emit_line(out, NULL, instr->reg, besm_cyr_mnem[k], a);
            break;
        case BESM_SHAPE_IMM0:
            snprintf(a, sizeof(a), "%d", instr->addr);
            emit_line(out, NULL, 0, besm_cyr_mnem[k], a);
            break;
        case BESM_SHAPE_IMMR:
            // `уиа` (VTM) carries a symbolic address when it loads a global's address into
            // an index register; otherwise the operand is a plain decimal immediate.
            if (instr->name)
                bemsh_operand(a, sizeof(a), instr);
            else
                snprintf(a, sizeof(a), "%d", instr->addr);
            emit_line(out, NULL, instr->reg, besm_cyr_mnem[k], a);
            break;
        case BESM_SHAPE_SPECIAL:
            emit_bemsh_special(out, instr);
            break;
        }
    }
}

static void emit_bemsh_block(FILE *out, const Besm_Block *block)
{
    for (; block; block = block->next)
        emit_bemsh_instr(out, block->body);
}

// Every старт…финиш is a self-contained Macro-Bemsh translation unit and must be wrapped in
// its own `ввд$$$` … `квч$$$/трн$$$/0-0/блмак/бтмалф/кнц$$$` deck: the БЕМШ translator
// processes exactly one module per deck, and besmc/the Dubna job add no such markers (it feeds
// the source verbatim after the `*bemsh` control card).  Multiple decks after one `*bemsh` all
// assemble, so a compiled TU with several modules emits several decks back to back.  This
// mirrors the hand-written libbem `.bemsh` files, each of which carries the same deck.
static void deck_open(FILE *out, const char *comment)
{
    fputs("ввд$$$\n", out);
    // A full-line comment is a `*` in column 1 (Bemsh).
    if (comment)
        fprintf(out, "* %s\n", comment);
    else
        fputs("*\n", out);
}

static void deck_close(FILE *out)
{
    fputs("квч$$$\nтрн$$$\n0-0\nблмак\nбтмалф\nкнц$$$\n", out);
}

static void emit_bemsh_func(FILE *out, const char *comment, const Besm_Func *func)
{
    for (; func; func = func->next) {
        deck_open(out, comment);
        emit_bemsh_block(out, func->blocks); // the block stream carries старт…финиш
        deck_close(out);
    }
}

static void emit_bemsh_data_section(FILE *out, const char *comment,
                                    const Besm_DataSection *section)
{
    for (; section; section = section->next) {
        deck_open(out, comment);
        emit_line(out, section->name, 0, "старт", "1"); // start-address operand (see BESM_STMT_NAME)
        emit_bemsh_instr(out, section->items);
        emit_line(out, NULL, 0, "финиш", "");
        deck_close(out);
    }
}

void emit_bemsh_module(FILE *out, const Besm_Module *module)
{
    emit_bemsh_func(out, module->comment, module->funcs);
    emit_bemsh_data_section(out, module->comment, module->sections);
}
