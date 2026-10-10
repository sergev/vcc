#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "internal.h"

// Encode a C double as a native BESM-6 48-bit floating-point word (see
// backend/besm6/Besm6_Data_Representation.md §6): bits 48-42 = 7-bit exponent biased by 64, bit 41
// = sign, bits 40-1 = 40-bit two's-complement mantissa.  b6as has no floating-point literal syntax,
// so the Unix emitter renders every real as its octal bit pattern.
static uint64_t unix_real_word(double v)
{
    if (v == 0.0)
        return 0; // machine zero is the all-zero word

    int e2;
    double f = frexp(v, &e2); // v = f * 2^e2, with 0.5 <= |f| < 1
    // A 41-bit two's-complement value T (bit 41 = sign, bits 40-1 = mantissa) with
    // T / 2^40 = f gives value = (T / 2^40) * 2^e2, i.e. biased exponent E = e2 + 64.
    int64_t T = llround(ldexp(f, 40));
    int E     = e2 + 64;
    if (T >= (INT64_C(1) << 40)) {
        // f rounded up to 1.0 (fraction not representable): renormalize.
        T >>= 1;
        E++;
    }
    // BESM-6 normalization requires the sign bit (41) to differ from bit 40.  frexp's
    // magnitude is [0.5, 1); a negative exact half (f == -0.5, T == -2^39) has bit 41 ==
    // bit 40 — un-normalized.  Renormalize to mantissa -1.0 (T == -2^40, one lower
    // exponent) so the bit pattern matches the hardware / Madlen assembler
    // (e.g. -1.0 = 0x810000000000, not 0x838000000000).
    if (T == -(INT64_C(1) << 39)) {
        T = -(INT64_C(1) << 40); // T * 2, one lower exponent
        E--;
    }
    if (E < 1 || E > 127)
        fatal_error("floating constant %g is outside the BESM-6 exponent range", v);

    uint64_t mant = (uint64_t)T & ((UINT64_C(1) << 41) - 1); // bits 41-1
    return ((uint64_t)E << 41) | mant;
}

//
// Unix (b6as) assembler emitter.  Renders the dialect-agnostic Besm_Module in the
// AT&T-style syntax accepted by b6as (backend/besm6/Besm6_Unix_Assembler.md): segment directives
// (.text/.data/.bss), a `[modreg] mnem operand` line format, `#`-pool constant operands,
// and a *translation* of the Madlen externals/relocation model into b6as's .globl/label/
// .word model.  The four regular operand shapes and the mnemonic table are shared with the
// Madlen emitter (besm_mnem.c); only the framing, the name sanitizer, and the special/
// directive handling differ.
//
// Runtime-helper names are canonical `$` in the IR (b$ret, b$save, …); b6as accepts `$` in
// names, so they pass through unchanged (whereas the Madlen emitter lowers `$`→`/`).
//

// The current output segment; b6as starts in .text.  A segment directive is emitted only
// when the segment must change (set_segment).
typedef enum {
    SEG_NONE,
    SEG_TEXT,
    SEG_DATA,
    SEG_BSS,
} SegKind;

static void set_segment(FILE *out, SegKind *cur, SegKind want)
{
    if (*cur == want)
        return;
    *cur = want;
    switch (want) {
    case SEG_TEXT:
        fprintf(out, "    .text\n");
        break;
    case SEG_DATA:
        fprintf(out, "    .data\n");
        break;
    case SEG_BSS:
        fprintf(out, "    .bss\n");
        break;
    case SEG_NONE:
        break;
    }
}

// Sanitize a b6as identifier.  b6as names may contain letters, digits, `_`, `.`, and `$`.
// Map `%`→`.` (compiler-generated labels, which b6as strips with -X) and, defensively,
// `/`→`$` (no `/` should reach here once helper names are canonical `$`).  No truncation.
static void unix_sanitize(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; *src && i + 1 < n; src++) {
        char c = *src;
        if (c == '%') {
            // Compiler-internal name (temp/local/label) -> '.'-led, which b6as -X strips.
            // A '.' immediately followed by a digit is a bit-mask literal (.N), not a name,
            // so a digit-leading body (a %N temporary) gets a 'T' inserted (".TN") to stay
            // a name — that also keeps it distinct from a letter-leading body ('.'+letter,
            // e.g. %L2 -> .L2 for a loop label).
            dst[i++] = '.';
            if (i + 1 < n && src[1] >= '0' && src[1] <= '9')
                dst[i++] = 'T';
            continue;
        }
        if (c == '/')
            c = '$';
        dst[i++] = c;
    }
    dst[i] = '\0';
}

// Build the address-field string from (name, addr): "name+N" / "name-N" / "name" / "N" /
// "" (empty when both are zero/null; buf must be pre-zeroed by the caller).
static void unix_addr(char *buf, size_t n, const char *name, int addr)
{
    char sname[64] = "";
    if (name)
        unix_sanitize(sname, sizeof(sname), name);
    if (name && addr > 0)
        snprintf(buf, n, "%s+%d", sname, addr);
    else if (name && addr < 0)
        snprintf(buf, n, "%s-%d", sname, -addr);
    else if (name)
        snprintf(buf, n, "%s", sname);
    else if (addr)
        snprintf(buf, n, "%d", addr);
}

// b6as reads a bare integer literal as decimal; a leading `0` selects octal.  For a value
// below 8 the two spellings coincide, so the octal marker is redundant: emit `7`, not `07`.
static const char *unix_octal_prefix(uint64_t word)
{
    return word < 8 ? "" : "0";
}

// Format instruction `i`'s address operand.  A structural constant (`i->konst`) becomes a
// b6as `#`-pool operand: `#<octal>` for an integer, `#<real>` for a floating value (the `#`
// operator pools+deduplicates the word in the const segment).  Otherwise the (name, addr)
// pair is rendered by unix_addr.
//
// A zero constant never reaches here: instruction selection leaves such an instruction with no
// operand at all, so it pools no word and reads memory word 0, which always holds zero.
static void unix_operand(char *buf, size_t n, const Besm_Instr *i)
{
    if (i->konst) {
        Besm_ConstWord w = besm_const_word(i->konst);
        uint64_t word    = w.is_real ? unix_real_word(w.real_val) : w.word;
        snprintf(buf, n, "#%s%" PRIo64, unix_octal_prefix(word), word);
        return;
    }
    // Madlen literal-address expressions (i->name begins with '='): b6as has no '='
    // syntax, so translate to a '#'-pool constant, which pools+dedups the word in the
    // const segment exactly as Madlen '=' does.  instr.c bakes these as octal integer
    // literals (=<octal>) plus left-aligned octal literals (=:<octal>, MSB-anchored — the
    // fat-pointer markers and the INT-format exponent word).
    if (i->name && i->name[0] == '=') {
        // Copy the octal digits, skipping any grouping spaces Madlen allows.
        const char *src = i->name + 1;
        char prefix     = '\0';
        if (*src == ':') {
            // =:<octal> is a Madlen left-aligned octal literal; b6as spells left-align as
            // the prefix-apostrophe form 0'<octal>.  =:64 -> #0'64.
            prefix = '\'';
            src++;
        }
        char digits[48];
        size_t j = 0;
        for (const char *p = src; *p && j + 1 < sizeof(digits); p++)
            if (*p != ' ')
                digits[j++] = *p;
        digits[j] = '\0';
        if (prefix)
            // The left-align form is only defined after an explicit base marker, so the
            // leading `0` stays even for a single digit.
            snprintf(buf, n, "#0'%s", digits);
        else
            // One octal digit is below 8, hence needs no octal marker.
            snprintf(buf, n, "#%s%s", (j == 1) ? "" : "0", digits);
        return;
    }
    unix_addr(buf, n, i->name, i->addr);
}

// Emit a label definition at column 0: `name:`.
static void emit_ulabel(FILE *out, const char *name)
{
    char s[64];
    unix_sanitize(s, sizeof(s), name);
    fprintf(out, "%s:\n", s);
}

// Emit a `.globl name` directive.
static void emit_uglobl(FILE *out, const char *name)
{
    char s[64];
    unix_sanitize(s, sizeof(s), name);
    fprintf(out, "    .globl %s\n", s);
}

// Emit a 4-space-indented directive: `.dir operand` (or bare `.dir` when operand is empty).
static void emit_udir(FILE *out, const char *dir, const char *operand)
{
    if (operand && operand[0])
        fprintf(out, "    %s %s\n", dir, operand);
    else
        fprintf(out, "    %s\n", dir);
}

// Emit one instruction line: `[ NN ]mnem[ operand]`.  Without a modreg the line is indented
// with 4 spaces; with one, a ` NN ` field (space + register right-aligned to width 2 + space)
// occupies the same 4 columns, so mnemonics line up regardless.
static void emit_uinstr(FILE *out, int mreg, const char *mnem, const char *operand)
{
    if (mreg)
        fprintf(out, " %2d ", mreg);
    else
        fputs("    ", out);
    fputs(mnem, out);
    if (operand && operand[0])
        fprintf(out, " %s", operand);
    fputc('\n', out);
}

// Emit a Z00 pointer-init word, consuming the z00a/z00b pair.  `z00a` is the high half
// (carries the optional datum `label` and, for a fat pointer, a KOI-7 byte-offset marker in
// `reg`); the following `z00b` carries the address (`name`+word offset in `addr`).  A plain
// pointer (z00a.reg == 0) coalesces into one relocatable `.word name+off`; a fat pointer
// emits the raw long opcode `@00` per half.  Returns the last node consumed (z00b) so the
// caller's loop advances past it.
static const Besm_Instr *emit_unix_z00(FILE *out, const Besm_Instr *z00a, SegKind *cur)
{
    set_segment(out, cur, SEG_DATA);
    const Besm_Instr *z00b = z00a->next;
    char a[64]             = "";
    if (z00b)
        unix_addr(a, sizeof(a), z00b->name, z00b->addr);
    if (z00a->label)
        emit_ulabel(out, z00a->label);
    if (z00a->reg == 0) {
        // Plain pointer: high half is zero → single relocatable .word.
        emit_udir(out, ".word", a);
    } else {
        // Fat pointer: byte-offset marker rides the leading modreg of the high half.
        emit_uinstr(out, (int)z00a->reg, "@00", "");
        emit_uinstr(out, 0, "@00", a);
    }
    return z00b ? z00b : z00a;
}

// Emit a BESM_SHAPE_SPECIAL instruction — the UTM/CALL irregular machine ops, the assembler
// directives, and the data pseudo-ops.  Externals/relocation are translated (not
// transliterated) into the b6as model.  Returns the last node consumed (normally `instr`;
// for a Z00 pair, its z00b) so the caller advances correctly.
static const Besm_Instr *emit_unix_special(FILE *out, const Besm_Instr *instr, SegKind *cur)
{
    char a[64] = "";
    switch (instr->kind) {
    // Index-register add: bare `utm` when the delta is zero.
    case BESM_REG_UTM:
        set_segment(out, cur, SEG_TEXT);
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_uinstr(out, (int)instr->reg, "utm", a);
        break;

    // Extracode: b6as has no mnemonic for opcodes 050-077, so they are written as the raw
    // short-address opcode `$NN` in octal (backend/besm6/Besm6_Unix_Assembler.md §9.3) — `$77 4` is
    // the v7 write syscall, as in libc/besm6/unix/write.s.  A raw opcode takes the same
    // operand forms as a named instruction, so the (reg, addr) pair renders as usual.
    case BESM_IO_EXTRACODE: {
        set_segment(out, cur, SEG_TEXT);
        char mnem[8];
        snprintf(mnem, sizeof(mnem), "$%o", instr->opcode);
        if (instr->addr)
            snprintf(a, sizeof(a), "%d", instr->addr);
        emit_uinstr(out, (int)instr->reg, mnem, a);
        break;
    }

    // A direct call is `13 vjm name` (,call, ≡ 13 ,vjm,, link register r13); b6as
    // auto-declares the undefined callee as external, so no .globl is needed.
    case BESM_BRANCH_CALL:
        set_segment(out, cur, SEG_TEXT);
        unix_sanitize(a, sizeof(a), instr->name);
        emit_uinstr(out, REG_RET, "vjm", a);
        break;

    // Subprogram name → define the label.  Whether the symbol is also exported depends on
    // the owning function's linkage, which only emit_unix_func knows; it emits the .globl
    // just ahead of this label.
    case BESM_STMT_NAME:
        set_segment(out, cur, SEG_TEXT);
        emit_ulabel(out, instr->name);
        break;

    // Secondary entry → export the symbol and define the label.
    case BESM_STMT_ENTRY:
        emit_uglobl(out, instr->name);
        emit_ulabel(out, instr->name);
        break;
    case BESM_STMT_LABEL:
        emit_ulabel(out, instr->name);
        break;

    // SUBP (external declaration), END (subprogram terminator), and BASE (relocatable
    // basing) have no b6as equivalent — undefined names auto-extern, segments are implicit,
    // and relocation is handled by the linker.  Drop them.
    case BESM_STMT_SUBP:
    case BESM_STMT_END:
    case BESM_STMT_BASE:
        break;

    // Data words.
    case BESM_DATA_LOG:
        set_segment(out, cur, SEG_DATA);
        if (instr->name)
            emit_ulabel(out, instr->name);
        if (instr->log_val == 0) {
            // Pack a run of unlabeled zero words, up to 8 per line.
            strcpy(a, "0");
            for (int n = 1; n < 8 && instr->next && instr->next->kind == BESM_DATA_LOG &&
                            !instr->next->name && instr->next->log_val == 0;
                 n++) {
                instr = instr->next;
                strcat(a, ", 0");
            }
            emit_udir(out, ".word", a);
            break;
        }
        snprintf(a, sizeof(a), "%s%" PRIo64, unix_octal_prefix(instr->log_val), instr->log_val);
        emit_udir(out, ".word", a);
        break;
    case BESM_DATA_REAL: {
        set_segment(out, cur, SEG_DATA);
        if (instr->name)
            emit_ulabel(out, instr->name);
        uint64_t word = unix_real_word(instr->real_val);
        snprintf(a, sizeof(a), "%s%" PRIo64, unix_octal_prefix(word), word);
        emit_udir(out, ".word", a);
        break;
    }
    case BESM_DATA_INT:
        set_segment(out, cur, SEG_DATA);
        if (instr->name)
            emit_ulabel(out, instr->name);
        snprintf(a, sizeof(a), "%d", instr->addr);
        emit_udir(out, ".word", a);
        break;
    case BESM_DATA_BSS:
        set_segment(out, cur, SEG_BSS);
        if (instr->name)
            emit_ulabel(out, instr->name);
        if (instr->addr)
            fprintf(out, "    . = . + %d\n", instr->addr);
        break;
    case BESM_DATA_Z00:
        return emit_unix_z00(out, instr, cur);

    default:
        internal_error("besm6: instruction kind %d has no Unix assembler form", (int)instr->kind);
    }
    return instr;
}

static void emit_unix_instr(FILE *out, const Besm_Instr *instr, SegKind *cur)
{
    while (instr) {
        char a[64]       = "";
        Besm_InstrKind k = instr->kind;
        switch (besm_operand_shape(k)) {
        case BESM_SHAPE_MEM:
            set_segment(out, cur, SEG_TEXT);
            unix_operand(a, sizeof(a), instr);
            emit_uinstr(out, (int)instr->reg, besm_latin_mnem[k], a);
            break;
        case BESM_SHAPE_IMM0:
            set_segment(out, cur, SEG_TEXT);
            snprintf(a, sizeof(a), "%d", instr->addr);
            emit_uinstr(out, 0, besm_latin_mnem[k], a);
            break;
        case BESM_SHAPE_IMMR:
            set_segment(out, cur, SEG_TEXT);
            // A symbolic `vtm` operand relocates like any other Format-2 address field.
            if (instr->name)
                unix_operand(a, sizeof(a), instr);
            else
                snprintf(a, sizeof(a), "%d", instr->addr);
            emit_uinstr(out, (int)instr->reg, besm_latin_mnem[k], a);
            break;
        case BESM_SHAPE_SPECIAL:
            instr = emit_unix_special(out, instr, cur);
            break;
        }
        instr = instr->next;
    }
}

static void emit_unix_func(FILE *out, const Besm_Func *func, SegKind *cur)
{
    for (; func; func = func->next) {
        // An internal-linkage (`static`) function stays unexported: its BESM_STMT_NAME still
        // defines the label, but no `.globl` precedes it, so b6as leaves the symbol local to
        // the object and two same-named statics in different objects cannot collide in b6ld.
        if (func->global) {
            set_segment(out, cur, SEG_TEXT);
            emit_uglobl(out, func->name);
        }
        for (const Besm_Block *block = func->blocks; block; block = block->next)
            emit_unix_instr(out, block->body, cur);
    }
}

static void emit_unix_data_section(FILE *out, const Besm_DataSection *section, SegKind *cur)
{
    for (; section; section = section->next) {
        // A named, external tentative definition (`int x;`, no initializer) becomes a `.comm`
        // common symbol so repeated tentative defs of the same name (and a real definition
        // elsewhere) merge at link time instead of colliding.  A zero-*initialized* def is a
        // strong .bss symbol, not a common, so it falls through to the generic path below.
        // `.comm` is segment-independent, so don't switch segments here — leave `cur` untouched.
        if (section->name && section->tentative && section->global) {
            int words = 0;
            for (const Besm_Instr *it = section->items; it; it = it->next)
                words += it->addr;
            char s[64];
            unix_sanitize(s, sizeof(s), section->name);
            fprintf(out, "    .comm %s, %d\n", s, words < 1 ? 1 : words);
            continue;
        }

        SegKind seg = section->kind == BESM_SK_BSS    ? SEG_BSS
                      : section->kind == BESM_SK_CODE ? SEG_TEXT
                                                      : SEG_DATA;
        set_segment(out, cur, seg);
        if (section->name) {
            // An internal-linkage (`static`) definition stays unexported: emit its label but
            // no `.globl`.  External definitions are declared global as before.
            if (section->global)
                emit_uglobl(out, section->name);
            emit_ulabel(out, section->name);
        }
        emit_unix_instr(out, section->items, cur);
    }
}

void emit_unix_module(FILE *out, const Besm_Module *module)
{
    if (module->comment)
        fprintf(out, "// %s\n", module->comment);

    SegKind cur = SEG_NONE;
    emit_unix_func(out, module->funcs, &cur);
    emit_unix_data_section(out, module->sections, &cur);
}
