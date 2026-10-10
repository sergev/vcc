#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "besm.h"
#include "internal.h"

// Dispatch to the per-dialect module emitter.
void besm_emit_module(FILE *out, const Besm_Module *module, Besm_Dialect dialect)
{
    switch (dialect) {
    case BESM_MADLEN:
        emit_madlen_module(out, module);
        break;
    case BESM_UNIX:
        emit_unix_module(out, module);
        break;
    case BESM_BEMSH:
        emit_bemsh_module(out, module);
        break;
    }
}

void mad_fresh_label(char *buf, size_t n, const char *prefix)
{
    static int counter = 0;
    snprintf(buf, n, "%s.%d", prefix, counter++);
}

// Format a double as the decimal-number field of a Madlen REAL constant.  Madlen
// requires a mandatory decimal point ("2." not "2"), placed before any E exponent.
void mad_format_real(char *buf, size_t n, double val)
{
    // 13 significant digits round-trip a BESM-6 40-bit mantissa exactly (2^40 ~ 1.1e12,
    // ~12.04 decimal digits); fewer would drop low mantissa bits.
    snprintf(buf, n, "%.13g", val);
    char *e = strpbrk(buf, "eE");
    if (!strchr(buf, '.') && !e) {
        // Bare integer.  Madlen's decimal-constant parser accumulates the mantissa into
        // a 40-bit field, so an integer mantissa >= 2^40 overflows ("ОШИБКА В АДРЕСЕ").
        // Re-emit such a value in exponent form, whose mantissa stays below 10.
        double a = val < 0 ? -val : val;
        if (a >= 1099511627776.0) { // 2^40
            snprintf(buf, n, "%.12e", val);
            e = strpbrk(buf, "eE");
        }
    }
    if (strchr(buf, '.'))
        return; // already has a decimal point
    // Madlen requires a decimal point in a real constant: insert one before any
    // exponent ("1e+20" -> "1.e+20"), or append it otherwise ("5" -> "5.").
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

// Sanitize a Madlen identifier: replace '_'→'*', '$'→'/', '%'→'*', truncate to 8 chars.
static void sanitize_name(char *dst, size_t n, const char *src)
{
    size_t lim = n - 1 < 8 ? n - 1 : 8;
    size_t i   = 0;
    for (; *src && i < lim; src++, i++) {
        char c = *src;
        if (c == '_')
            c = '*';
        else if (c == '$')
            c = '/';
        else if (c == '%')
            c = '*';
        dst[i] = c;
    }
    dst[i] = '\0';
}

//
// Build the address-field string from (name, addr).
// name+addr → "name+N", name-addr → "name-N", name only → "name",
// addr only → "N", both zero/null → "" (buf already zeroed by caller).
//
// Names beginning with '=' are Madlen literal-address expressions (=N, =rX),
// not identifiers; they are passed through without sanitization or truncation.
//
static void addr_str(char *buf, size_t n, const char *name, int addr)
{
    char sname[9];
    const char *aname = name;
    if (name && name[0] != '=') {
        sanitize_name(sname, sizeof(sname), name);
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
// Format instruction `i`'s address operand into `buf`.  A structural constant
// (`i->konst`) becomes a Madlen literal-address expression: `=octal` for an integer,
// `=r<value>` for a real.  Otherwise the (name, addr) pair is rendered by addr_str.
//
// A zero constant never reaches here: instruction selection leaves such an instruction with
// no operand at all, so it reads memory word 0 (`,xta,` is the documented idiom for A <- 0).
//
static void mad_operand(char *buf, size_t n, const Besm_Instr *i)
{
    if (i->konst) {
        Besm_ConstWord w = besm_const_word(i->konst);
        if (w.is_real) {
            char num[48];
            mad_format_real(num, sizeof(num), w.real_val);
            snprintf(buf, n, "=r%s", num);
        } else {
            snprintf(buf, n, "=%" PRIo64, w.word);
        }
        return;
    }
    addr_str(buf, n, i->name, i->addr);
}

//
// The Madlen mnemonic for kind `k`.  Madlen shares the Latin mnemonic table with the Unix
// (b6as) emitter, with one exception: the 1972 autocode names no halt — `,stop,` is rejected
// with "ошибка в коп" — so the halt is written as the raw octal machine code Madlen accepts in
// place of any mnemonic (backend/besm6/Madlen.md §4, "Octal Machine Codes").
//
// The digit count picks the instruction format, and both formats have an opcode 033: two
// digits, `,33,`, is the Format-2 (long-address) 033 — the halt — while three digits, `,033,`,
// is the Format-1 033, which is `ext` and faults as a privileged instruction.  The raw form
// forfeits automatic basing, which the halt does not want anyway: its address field carries a
// halt code, not a memory address.
//
static const char *mad_mnem(Besm_InstrKind k)
{
    return (k == BESM_BRANCH_STOP) ? "33" : besm_latin_mnem[k];
}

//
// Emit one Madlen statement line.
//
static void emit_line(FILE *out, const char *label, int mreg, const char *mnem, const char *addr)
{
    if (label) {
        char sl[9];
        sanitize_name(sl, sizeof(sl), label);
        fprintf(out, " %8s:", sl);
    } else {
        fprintf(out, "          ");
    }
    if (mreg) {
        fprintf(out, "%2d ", mreg);
    } else {
        fprintf(out, "   ");
    }
    fprintf(out, ",%s,", mnem);
    if (addr && addr[0])
        fprintf(out, " %s", addr);
    fputc('\n', out);
}

//
// Emit a BESM_SHAPE_SPECIAL instruction — the ones whose Madlen spelling is not a plain
// machine mnemonic + regular operand: UTM (operand suppressed when zero), CALL/BASE
// (a sanitized name operand), the assembler directives, and the data pseudo-ops.
//
static void emit_madlen_special(FILE *out, const Besm_Instr *instr)
{
    char a[64] = "";
    switch (instr->kind) {
    // Index-register add: bare `utm` when the delta is zero.
    case BESM_REG_UTM:
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, NULL, instr->reg, "utm", a);
        break;

    // Extracode: the mnemonic is the opcode itself, written `*NN` in octal (`,*71,` is the
    // print-line extracode, as in libc/besm6/madlen/b_tout.madlen).  Unlike the raw octal
    // machine code the halt uses, this is a symbolic mnemonic, so it keeps automatic basing.
    // The effective address is the ordinary (reg, addr) pair, rendered in decimal.
    case BESM_IO_EXTRACODE: {
        char mnem[8];
        snprintf(mnem, sizeof(mnem), "*%o", instr->opcode);
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, NULL, instr->reg, mnem, a);
        break;
    }

    // Call / basing take a sanitized name in the operand field.
    case BESM_BRANCH_CALL:
        sanitize_name(a, sizeof(a), instr->name);
        emit_line(out, NULL, 0, "call", a);
        break;
    case BESM_STMT_BASE:
        sanitize_name(a, sizeof(a), instr->name);
        emit_line(out, NULL, instr->reg, "base", a);
        break;

    // Assembly directives — the name goes in the label field.
    case BESM_STMT_LABEL:
        emit_line(out, instr->name, 0, "bss", "");
        break;
    case BESM_STMT_NAME:
        emit_line(out, instr->name, 0, "name", "");
        break;
    case BESM_STMT_SUBP:
        emit_line(out, instr->name, 0, "subp", "");
        break;
    case BESM_STMT_ENTRY:
        emit_line(out, instr->name, 0, "entry", "");
        break;
    case BESM_STMT_END:
        emit_line(out, NULL, 0, "end", "");
        break;

    // Data section directives.
    case BESM_DATA_LOG:
        snprintf(a, sizeof(a), "%" PRIo64, instr->log_val);
        emit_line(out, instr->name, 0, "log", a);
        break;
    case BESM_DATA_BSS:
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, instr->name, 0, "bss", a);
        break;
    case BESM_DATA_INT:
        snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, instr->name, 0, "int", a);
        break;
    case BESM_DATA_REAL:
        mad_format_real(a, sizeof(a), instr->real_val);
        emit_line(out, instr->name, 0, "real", a);
        break;
    case BESM_DATA_EQU:
        snprintf(a, sizeof(a), "%d", instr->addr);
        emit_line(out, NULL, 0, "equ", a);
        break;
    case BESM_DATA_REF:
        sanitize_name(a, sizeof(a), instr->name);
        emit_line(out, NULL, 0, "oct", a);
        break;
    case BESM_DATA_STRING: {
        const char *s = instr->name;
        while (*s) {
            snprintf(a, sizeof(a), "%d", (unsigned char)*s++);
            emit_line(out, NULL, 0, "int", a);
        }
        emit_line(out, NULL, 0, "int", "0");
        break;
    }
    case BESM_DATA_Z00:
        mad_operand(a, sizeof(a), instr);
        emit_line(out, instr->label, instr->reg, "z00", a);
        break;

    default:
        internal_error("besm6: instruction kind %d has no Madlen form", (int)instr->kind);
    }
}

void emit_madlen_instr(FILE *out, const Besm_Instr *instr)
{
    for (; instr; instr = instr->next) {
        char a[64]       = "";
        Besm_InstrKind k = instr->kind;
        switch (besm_operand_shape(k)) {
        case BESM_SHAPE_MEM:
            mad_operand(a, sizeof(a), instr);
            emit_line(out, NULL, instr->reg, mad_mnem(k), a);
            break;
        case BESM_SHAPE_IMM0:
            snprintf(a, sizeof(a), "%d", instr->addr);
            emit_line(out, NULL, 0, mad_mnem(k), a);
            break;
        case BESM_SHAPE_IMMR:
            // `vtm` carries a symbolic address when it loads the address of a global into
            // an index register; its 15-bit Format-2 field holds a relocatable name just
            // as `utc`'s does.  Otherwise the operand is a plain decimal immediate.
            if (instr->name)
                mad_operand(a, sizeof(a), instr);
            else
                snprintf(a, sizeof(a), "%d", instr->addr);
            // The mode-word write (`__besm6_maskpsw`) is a `vtm` whose modifier register is 0
            // — that is what makes it a PSW write rather than an index-register load.  Madlen
            // will not spell it: `,vtm,` with modifier 0, absent or written out, is rejected
            // with "ошибка в модификаторе" (it accepts only 1-15, for `,utm,` too).  So the
            // mode write goes out as raw octal machine code, exactly as the halt does above —
            // `,24, N` is the Format-2 opcode 024 with a zero register field, verified against
            // Bemsh's `уиа 1027`, which the translator encodes as `00 24 02003`.
            if (k == BESM_REG_VTM && instr->reg == 0)
                emit_line(out, NULL, 0, "24", a);
            else
                emit_line(out, NULL, instr->reg, mad_mnem(k), a);
            break;
        case BESM_SHAPE_SPECIAL:
            emit_madlen_special(out, instr);
            break;
        }
    }
}

void emit_madlen_block(FILE *out, const Besm_Block *block)
{
    for (; block; block = block->next)
        emit_madlen_instr(out, block->body);
}

void emit_madlen_func(FILE *out, const Besm_Func *func)
{
    for (; func; func = func->next)
        emit_madlen_block(out, func->blocks);
}

void emit_madlen_data_section(FILE *out, const Besm_DataSection *section)
{
    for (; section; section = section->next) {
        emit_line(out, section->name, 0, "name", "");
        emit_madlen_instr(out, section->items);
        emit_line(out, NULL, 0, "end", "");
    }
}

void emit_madlen_module(FILE *out, const Besm_Module *module)
{
    if (module->comment)
        fprintf(out, "c  %s\n", module->comment);
    else
        fprintf(out, "c\n");

    emit_madlen_func(out, module->funcs);
    emit_madlen_data_section(out, module->sections);
}
