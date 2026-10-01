#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "internal.h"
#include "tac.h"
#include "utf8_to_koi7.h"
#include "xalloc.h"

static void insert_before_end(Besm_Block *block, Besm_Instr *chain);

//
// Type sizes (in 48-bit words).
// BESM-6 is word-addressed; every C scalar and pointer fits in one word.
// Arrays occupy element_size * N consecutive words.  A structure's TAC size is in
// target bytes, so it is divided by the word size (rounded up) to yield words.
// Alignment is always 1 (no sub-word alignment requirement).
//
static int codegen_sizeof(const Tac_Type *t)
{
    switch (t->kind) {
    case TAC_TYPE_SCHAR:
    case TAC_TYPE_UCHAR:
    case TAC_TYPE_SHORT:
    case TAC_TYPE_USHORT:
    case TAC_TYPE_INT:
    case TAC_TYPE_UINT:
    case TAC_TYPE_LONG:
    case TAC_TYPE_ULONG:
    case TAC_TYPE_LONG_LONG:
    case TAC_TYPE_ULONG_LONG:
    case TAC_TYPE_FLOAT:
    case TAC_TYPE_DOUBLE:
    case TAC_TYPE_LONG_DOUBLE:
    case TAC_TYPE_POINTER:
        return 1;
    case TAC_TYPE_ARRAY:
        if (t->u.array.elem_type->kind == TAC_TYPE_SCHAR ||
            t->u.array.elem_type->kind == TAC_TYPE_UCHAR)
            return (t->u.array.size + 5) / 6;
        return codegen_sizeof(t->u.array.elem_type) * t->u.array.size;
    case TAC_TYPE_STRUCTURE:
        return (t->u.structure.size + BESM6_WORD_BYTES - 1) / BESM6_WORD_BYTES;
    default:
        return 1;
    }
}

// Encode `len` source bytes into static data bytes, returning the encoded byte count in
// *out_len.  Madlen/Bemsh target the Dubna monitor, which stores text in KOI-7; the Unix
// (b6as) dialect keeps the raw source bytes so a program's write(1,…) reaches the host
// stdout unchanged.  KOI-7 conversion never expands the byte count, so len+1 sizes both
// cases — but it may *shrink* it (a multi-byte UTF-8 sequence becomes one byte), which is
// why the encoded length is returned rather than measured.  It cannot be measured in any
// case: a decoded literal may hold embedded NULs.  Caller frees the result with xfree.
static char *encode_static_string(const char *src, size_t len, size_t *out_len,
                                  Besm_Dialect dialect)
{
    char *dst = xalloc(len + 1, __func__, __FILE__, __LINE__);
    if (dialect == BESM_UNIX) {
        memcpy(dst, src, len);
        *out_len = len;
    } else {
        *out_len = utf8_to_koi7_n(src, len, dst);
    }
    dst[*out_len] = 0;
    return dst;
}

// The decoded bytes of a string static init, and their count.  A re-imported empty string
// literal has a NULL `val` (a zero-length wio blob); treat it as the empty string.
static const char *string_init_bytes(const Tac_StaticInit *init, size_t *len)
{
    *len = init->u.string.val ? init->u.string.len : 0;
    return init->u.string.val ? init->u.string.val : "";
}

// True for an array whose innermost element is a character type — its bytes are
// packed 6-per-word (see codegen_sizeof), so its static init list must be packed
// the same way rather than emitted one word per element.
static bool is_char_array(const Tac_Type *t)
{
    if (t->kind != TAC_TYPE_ARRAY)
        return false;
    const Tac_Type *e = t->u.array.elem_type;
    while (e->kind == TAC_TYPE_ARRAY)
        e = e->u.array.elem_type;
    return e->kind == TAC_TYPE_SCHAR || e->kind == TAC_TYPE_UCHAR;
}

// Byte count an init item contributes to a packed character array.  A string's
// KOI-7 length can be shorter than its nominal (UTF-8) source length, so the byte
// count is taken from the converted data rather than from the array's type size.
static size_t char_init_item_bytes(const Tac_StaticInit *init, Besm_Dialect dialect)
{
    switch (init->kind) {
    case TAC_STATIC_INIT_I8:
    case TAC_STATIC_INIT_U8:
        return 1;
    case TAC_STATIC_INIT_ZERO:
        return (size_t)init->u.zero_bytes;
    case TAC_STATIC_INIT_STRING: {
        size_t len, enc_len;
        const char *src = string_init_bytes(init, &len);
        char *enc       = encode_static_string(src, len, &enc_len, dialect);
        size_t nb       = enc_len + (init->u.string.null_terminated ? 1 : 0);
        xfree(enc);
        return nb;
    }
    default:
        fatal_error("unexpected static init kind %d in char array", (int)init->kind);
        return 0;
    }
}

// Build a chain of `n` explicit all-zero data words (`,log, 0`).  Used for static
// *locals*, whose zero padding cannot be a `,bss,` reservation: a static local's data
// is spliced into the function's code module, and the Dubna loader does not zero `,bss,`
// space there (only a separate global BSS section is loader-zeroed), so the words would
// read back as garbage.  Explicit zero words become part of the loaded module image.
static Besm_Instr *zero_log_words(int n)
{
    Besm_Instr *head = NULL, **tail = &head;
    for (int i = 0; i < n; i++) {
        Besm_Instr *si = besm_new_instr(BESM_DATA_LOG);
        si->log_val    = 0;
        *tail          = si;
        tail           = &si->next;
    }
    return head;
}

// Flatten a character array's static init list (byte values, zero byte-runs, and
// embedded string literals) into one byte buffer packed 6 bytes per word.  Data
// words are emitted as BESM_DATA_LOG; complete all-zero words at the tail are
// coalesced into a single BESM_DATA_BSS (matching the convention for zero padding) —
// unless `zero_as_words` (static local), where they become explicit `,log, 0` words.
static Besm_Instr *char_array_log_items(const Tac_StaticInit *init, bool zero_as_words,
                                        Besm_Dialect dialect)
{
    size_t total = 0;
    for (const Tac_StaticInit *it = init; it; it = it->next)
        total += char_init_item_bytes(it, dialect);
    if (total == 0)
        total = 1;

    // The packing loop below reads whole 6-byte words, so the buffer must be padded up
    // to a word boundary; otherwise the final word's tail bytes read past the allocation.
    size_t bufsz       = ((total + 5) / 6) * 6;
    unsigned char *buf = xalloc(bufsz, __func__, __FILE__, __LINE__);
    memset(buf, 0, bufsz);
    // `data_end` is the byte offset where the trailing run of explicit ZERO padding
    // begins.  Bytes from string/value items (even zero ones, like a string's null
    // terminator) are emitted as data words; only whole words past `data_end` become
    // a single BSS reservation.
    size_t pos = 0, data_end = 0;
    for (const Tac_StaticInit *it = init; it; it = it->next) {
        if (it->kind == TAC_STATIC_INIT_STRING) {
            size_t len, enc_len;
            const char *src = string_init_bytes(it, &len);
            char *enc       = encode_static_string(src, len, &enc_len, dialect);
            size_t nb       = enc_len + (it->u.string.null_terminated ? 1 : 0);
            for (size_t i = 0; i < nb && pos + i < total; i++)
                buf[pos + i] = (unsigned char)enc[i];
            pos += nb;
            data_end = pos;
            xfree(enc);
        } else if (it->kind == TAC_STATIC_INIT_I8) {
            if (pos < total)
                buf[pos] = (uint8_t)it->u.char_val;
            pos++;
            data_end = pos;
        } else if (it->kind == TAC_STATIC_INIT_U8) {
            if (pos < total)
                buf[pos] = it->u.uchar_val;
            pos++;
            data_end = pos;
        } else { // TAC_STATIC_INIT_ZERO
            pos += (size_t)it->u.zero_bytes; // buffer already zeroed; not counted as data
        }
    }

    size_t n_words   = (total + 5) / 6;
    size_t log_words = (data_end + 5) / 6; // words covering the string/value data

    Besm_Instr *head = NULL, **tail = &head;
    for (size_t w = 0; w < log_words; w++) {
        uint64_t word = 0;
        for (int b = 0; b < 6; b++)
            word = (word << 8) | buf[w * 6 + b];
        Besm_Instr *si = besm_new_instr(BESM_DATA_LOG);
        si->log_val    = word;
        *tail          = si;
        tail           = &si->next;
    }
    long zero_words = (long)n_words - (long)log_words;
    if (zero_words > 0) {
        if (zero_as_words) {
            *tail = zero_log_words((int)zero_words);
        } else {
            Besm_Instr *bss = besm_new_instr(BESM_DATA_BSS);
            bss->addr       = (int)zero_words;
            *tail           = bss;
        }
    }
    xfree(buf);
    return head;
}

static uint64_t static_init_log_val(const Tac_StaticInit *init)
{
    switch (init->kind) {
    case TAC_STATIC_INIT_I8:
        return (uint64_t)(uint8_t)init->u.char_val;
    case TAC_STATIC_INIT_U8:
        return (uint64_t)init->u.uchar_val;
    case TAC_STATIC_INIT_I16:
        return (uint64_t)(int64_t)init->u.short_val & 0x1FFFFFFFFFF;
    case TAC_STATIC_INIT_I32:
        return (uint64_t)(int64_t)init->u.int_val & 0x1FFFFFFFFFF;
    case TAC_STATIC_INIT_I64:
        return (uint64_t)init->u.long_val & 0x1FFFFFFFFFF;
    case TAC_STATIC_INIT_U16:
        return (uint64_t)init->u.ushort_val;
    case TAC_STATIC_INIT_U32:
        return (uint64_t)init->u.uint_val;
    case TAC_STATIC_INIT_U64:
        return init->u.ulong_val & 0xFFFFFFFFFFFF;
    default:
        fatal_error("non-integer static init in log_val");
    }
}

// Append the data directive(s) for one word-sized, word-aligned static init item — a
// multi-byte integer, a float, or a pointer relocation.  Shared by the flat generic path
// and the struct packer; the byte-packed kinds (I8/U8/STRING/ZERO) are handled by callers.
static void append_word_item(const Tac_StaticInit *init, Besm_Instr ***tailp)
{
    Besm_Instr **tail = *tailp;
    Besm_Instr *item;
    switch (init->kind) {
    case TAC_STATIC_INIT_I16:
    case TAC_STATIC_INIT_I32:
    case TAC_STATIC_INIT_I64:
    case TAC_STATIC_INIT_U16:
    case TAC_STATIC_INIT_U32:
    case TAC_STATIC_INIT_U64:
        item          = besm_new_instr(BESM_DATA_LOG);
        item->log_val = static_init_log_val(init);
        break;
    case TAC_STATIC_INIT_POINTER: {
        int byte_offset = init->u.pointer.byte_offset;
        if (byte_offset % 6 != 0)
            fatal_error("Pointer byte offset is not a multiple of word size");
        Besm_Instr *subp = besm_new_instr(BESM_STMT_SUBP);
        subp->name       = xstrdup(init->u.pointer.name);
        *tail            = subp;
        tail             = &subp->next;
        Besm_Instr *z00a = besm_new_instr(BESM_DATA_Z00);
        *tail            = z00a;
        tail             = &z00a->next;
        Besm_Instr *z00b = besm_new_instr(BESM_DATA_Z00);
        z00b->name       = xstrdup(init->u.pointer.name);
        z00b->addr       = byte_offset / 6;
        *tail            = z00b;
        tail             = &z00b->next;
        *tailp           = tail;
        return;
    }
    case TAC_STATIC_INIT_FAT_POINTER: {
        int byte_off     = init->u.pointer.byte_offset;
        Besm_Instr *subp = besm_new_instr(BESM_STMT_SUBP);
        subp->name       = xstrdup(init->u.pointer.name);
        *tail            = subp;
        tail             = &subp->next;
        Besm_Instr *z00a = besm_new_instr(BESM_DATA_Z00);
        z00a->reg        = 8 + (unsigned)(5 - byte_off % 6);
        *tail            = z00a;
        tail             = &z00a->next;
        Besm_Instr *z00b = besm_new_instr(BESM_DATA_Z00);
        z00b->name       = xstrdup(init->u.pointer.name);
        z00b->addr       = byte_off / 6;
        *tail            = z00b;
        tail             = &z00b->next;
        *tailp           = tail;
        return;
    }
    case TAC_STATIC_INIT_FLOAT:
        item           = besm_new_instr(BESM_DATA_REAL);
        item->real_val = init->u.float_val;
        break;
    case TAC_STATIC_INIT_DOUBLE:
        item           = besm_new_instr(BESM_DATA_REAL);
        item->real_val = init->u.double_val;
        break;
    case TAC_STATIC_INIT_LONG_DOUBLE:
        // long double ≡ double on BESM-6 (one 48-bit native-FP word).
        item           = besm_new_instr(BESM_DATA_REAL);
        item->real_val = (double)init->u.long_double_val;
        break;
    default:
        // Unreachable: byte-packed kinds (I8/U8/STRING/ZERO) never reach here.
        fatal_error("internal error: unhandled word static init kind %d", (int)init->kind);
    }
    *tail  = item;
    *tailp = &item->next;
}

// True for a byte-packed static init item (occupies sub-word storage in a packed aggregate).
static bool is_byte_init(const Tac_StaticInit *it)
{
    return it->kind == TAC_STATIC_INIT_I8 || it->kind == TAC_STATIC_INIT_U8 ||
           it->kind == TAC_STATIC_INIT_STRING;
}

// True for a type whose static layout may place a char member at a non-word byte offset —
// a struct/union, or an array whose innermost element is a struct/union.  (A char-innermost
// array is packed by char_array_log_items; a word-element array and a scalar need no packing.)
static bool needs_byte_packing(const Tac_Type *t)
{
    while (t->kind == TAC_TYPE_ARRAY)
        t = t->u.array.elem_type;
    return t->kind == TAC_TYPE_STRUCTURE;
}

// Append `buf[lo, hi)` (word-aligned bounds) as packed BESM_DATA_LOG words, 6 bytes per
// word big-endian (byte #0 in the MSB — the packed-char member convention from instr.c).
static void flush_packed_words(const unsigned char *buf, int lo, int hi, Besm_Instr ***tailp)
{
    for (int w = lo; w < hi; w += 6) {
        uint64_t word = 0;
        for (int b = 0; b < 6; b++)
            word = (word << 8) | buf[w + b];
        Besm_Instr *si = besm_new_instr(BESM_DATA_LOG);
        si->log_val    = word;
        **tailp        = si;
        *tailp         = &si->next;
    }
}

// Pack a struct/union (or array-of-struct) static-init list that may interleave sub-word
// char members with word-aligned members.  Char bytes accumulate into a byte buffer packed
// 6-per-word; a ZERO run advances the byte cursor; a word-sized member — which a valid C
// layout always places on a word boundary — first flushes the pending byte word(s) as LOG
// data, then emits its own directive.  A trailing all-zero word run is coalesced into a
// single `,bss,` (file scope) or explicit zero words (static local), like the other paths.
static Besm_Instr *struct_log_items(const Tac_StaticInit *init, bool zero_as_words,
                                    Besm_Dialect dialect)
{
    int total = 0;
    for (const Tac_StaticInit *it = init; it; it = it->next) {
        if (it->kind == TAC_STATIC_INIT_ZERO)
            total += it->u.zero_bytes;
        else if (is_byte_init(it))
            total += (int)char_init_item_bytes(it, dialect);
        else
            total += 6; // a word-sized, word-aligned member
    }
    int nwords = (total + 5) / 6;
    if (nwords == 0)
        nwords = 1;
    unsigned char *buf = xalloc((size_t)nwords * 6, __func__, __FILE__, __LINE__);
    memset(buf, 0, (size_t)nwords * 6);

    Besm_Instr *head = NULL, **tail = &head;
    int pos = 0, pstart = 0, data_end = 0; // byte cursor, start of pending word run, last data byte

    for (const Tac_StaticInit *it = init; it; it = it->next) {
        if (it->kind == TAC_STATIC_INIT_I8) {
            buf[pos++] = (uint8_t)it->u.char_val;
            data_end   = pos;
        } else if (it->kind == TAC_STATIC_INIT_U8) {
            buf[pos++] = it->u.uchar_val;
            data_end   = pos;
        } else if (it->kind == TAC_STATIC_INIT_STRING) {
            size_t len, enc_len;
            const char *src = string_init_bytes(it, &len);
            char *enc       = encode_static_string(src, len, &enc_len, dialect);
            int nb          = (int)enc_len + (it->u.string.null_terminated ? 1 : 0);
            for (int i = 0; i < nb; i++)
                buf[pos + i] = (unsigned char)enc[i];
            pos += nb;
            data_end = pos;
            xfree(enc);
        } else if (it->kind == TAC_STATIC_INIT_ZERO) {
            pos += it->u.zero_bytes; // buffer already zeroed
        } else {
            // Word-aligned member: flush the pending char word(s), then emit it.
            flush_packed_words(buf, pstart, pos, &tail);
            append_word_item(it, &tail);
            pos += 6;
            pstart   = pos;
            data_end = pos;
        }
    }
    // Final flush: LOG words covering data, then a coalesced all-zero tail.
    int log_end = ((data_end + 5) / 6) * 6;
    if (log_end > nwords * 6)
        log_end = nwords * 6;
    flush_packed_words(buf, pstart, log_end, &tail);
    int zero_words = (nwords * 6 - log_end) / 6;
    if (zero_words > 0) {
        if (zero_as_words) {
            *tail = zero_log_words(zero_words);
        } else {
            Besm_Instr *bss = besm_new_instr(BESM_DATA_BSS);
            bss->addr       = zero_words;
            *tail           = bss;
        }
    }
    xfree(buf);
    return head;
}

// True when an initializer list is entirely zero fill (a single or repeated ZERO run, as the
// frontend lowers `int x = 0;` / `T a[N] = {0};`).  Such a definition carries no data words
// and belongs in .bss rather than .data.
static bool static_init_all_zero(const Tac_StaticInit *init)
{
    for (; init; init = init->next)
        if (init->kind != TAC_STATIC_INIT_ZERO)
            return false;
    return true;
}

// Build the chain of data directives for a static object of the given type and init list.
// `init == NULL` reserves zeroed storage.  The returned items carry no label; callers
// attach one as needed (a data section's `,name,`, or the first item for a static local
// emitted inside a function module).
//
// `zero_as_words` controls how zero storage is emitted.  A `,bss,` reservation (false) is
// the loader-zeroed default.  Explicit `,log, 0` words (true) are required whenever a
// `,bss,` gap would not stay contiguous with the object's data words: a static *local*
// (spliced into the function's code module, where `,bss,` space is left uninitialized —
// see `zero_log_words`), and — in the Unix dialect — a *partially-initialized* file-scope
// object, whose data words go to `.data` while a `,bss,` gap would land in the separate
// `.bss` section and split the object's storage (a whole-object `.bss` or a single Madlen
// `,name,` module both stay contiguous, so those keep the compact `,bss,` reservation).
static Besm_Instr *static_data_items(const Tac_Type *type, const Tac_StaticInit *init,
                                     bool zero_as_words, Besm_Dialect dialect)
{
    if (init == NULL) {
        if (zero_as_words)
            return zero_log_words(codegen_sizeof(type));
        Besm_Instr *item = besm_new_instr(BESM_DATA_BSS);
        item->addr       = codegen_sizeof(type);
        return item;
    }

    // A character array packs 6 bytes per word, so its whole init list (byte values,
    // zero runs, embedded strings) is flattened into a packed byte stream rather than
    // emitted one word per element.
    if (is_char_array(type))
        return char_array_log_items(init, zero_as_words, dialect);

    // A struct/union (or array thereof) may place a char member at a non-word byte offset,
    // so its mixed init list is byte-packed the same way (char members share a word).
    if (needs_byte_packing(type))
        return struct_log_items(init, zero_as_words, dialect);

    // Scalars and word-element arrays: one directive per item.  A standalone char scalar
    // (I8/U8) is one full word with its value in the low byte (byte #5) — the standalone-char
    // convention, distinct from the MSB-first packing used for char members above.
    Besm_Instr *head  = NULL;
    Besm_Instr **tail = &head;
    for (; init; init = init->next) {
        switch (init->kind) {
        case TAC_STATIC_INIT_I8:
        case TAC_STATIC_INIT_U8: {
            Besm_Instr *item = besm_new_instr(BESM_DATA_LOG);
            item->log_val    = static_init_log_val(init);
            *tail            = item;
            tail             = &item->next;
            break;
        }
        case TAC_STATIC_INIT_ZERO:
            if (zero_as_words)
                *tail = zero_log_words((init->u.zero_bytes + 5) / 6);
            else {
                Besm_Instr *item = besm_new_instr(BESM_DATA_BSS);
                item->addr       = (init->u.zero_bytes + 5) / 6;
                *tail            = item;
            }
            while (*tail)
                tail = &(*tail)->next;
            break;
        case TAC_STATIC_INIT_STRING:
            // Char-array init (e.g. `char arr[] = "ABC"`); the section NAME
            // already labels the first word, so no per-item label here.
            *tail = besm_string_log_items(init, NULL, dialect);
            while (*tail)
                tail = &(*tail)->next;
            break;
        default:
            append_word_item(init, &tail);
            break;
        }
    }
    return head;
}

void codegen_static_variable(const Tac_TopLevel *program, const Tac_TopLevel *tl, FILE *out,
                             Besm_Dialect dialect)
{
    // Skip a tentative definition when another toplevel defines (or already tentatively
    // reserves) the same name — see tac_static_superseded.  Without this, the streaming
    // frontend's separate tentative + initialized toplevels emit two strong labels of the
    // same name, which the Unix assembler rejects as a duplicate symbol.
    if (tac_static_superseded(program, tl))
        return;

    const char *name           = tl->u.static_variable.name;
    const Tac_StaticInit *init = tl->u.static_variable.init_list;

    // A tentative definition (no initializer) or one whose initializer is entirely zero fill
    // belongs in .bss: the storage is loader-zeroed, so no explicit .data words are emitted.
    // Both cases produce a BSS section so the symbol's label binds in .bss (not .data) — a
    // .data label with .bss storage would count as zero data bytes and, in the Unix dialect,
    // alias the first `.comm` common (placed at data_origin + data_size).  Only a truly
    // tentative def becomes a `.comm` common; a zero-initialized def is a strong .bss symbol.
    bool tentative           = (init == NULL);
    bool zero_bss            = tentative || static_init_all_zero(init);
    Besm_Module *module      = besm_new_module(name);
    Besm_DataSection *section = besm_new_data_section(zero_bss ? BESM_SK_BSS : BESM_SK_DATA);
    section->name             = xstrdup(name);
    section->global           = tl->u.static_variable.global;
    section->tentative        = tentative;
    // A partially-initialized file-scope object emits data words to `.data`; in the Unix
    // dialect its zero fill must also be `.data` words, since a `.bss` gap would land in a
    // separate section and split the object (see static_data_items).  A whole-object BSS
    // (zero_bss) or a single contiguous Madlen module keeps the compact `,bss,` reservation.
    bool zero_words = !zero_bss && dialect == BESM_UNIX;
    section->items = static_data_items(tl->u.static_variable.type, init, zero_words, dialect);
    module->sections          = section;

    besm_fold_string_constants(module, program, dialect);
    besm_emit_module(out, module, dialect);
    besm_free_module(module);
}

// True for a data directive that stores a word and can therefore carry a Madlen label in
// its `name` field (unlike SUBP, which declares an external, or Z00, which uses `name` as
// its operand).
static bool data_item_labelable(const Besm_Instr *item)
{
    switch (item->kind) {
    case BESM_DATA_LOG:
    case BESM_DATA_BSS:
    case BESM_DATA_REAL:
    case BESM_DATA_INT:
        return true;
    default:
        return false;
    }
}

// Find the first instruction in `items` that can carry the static local's Madlen label:
// either a directly-labelable data word (scalar/array/string), or — for a pointer/fat-pointer
// initializer, whose chain begins with a SUBP external declaration — the first Z00 address
// word, which carries its label in the dedicated `label` field (its `name` is the operand).
static Besm_Instr *static_local_label_site(Besm_Instr *items)
{
    if (items && data_item_labelable(items))
        return items;
    for (Besm_Instr *it = items; it; it = it->next)
        if (it->kind == BESM_DATA_Z00)
            return it;
    return NULL;
}

// Emit each block-scope static local of `fn` as a module-local labeled datum, spliced into
// the function module just before its `,end,` (after the code).  String constants referenced
// by a static-local initializer are folded in by the caller's besm_fold_string_constants.
void besm_emit_static_locals(Besm_Module *module, const Tac_TopLevel *fn, Besm_Dialect dialect)
{
    if (!module->funcs)
        return;
    Besm_Block *last = module->funcs->blocks;
    while (last->next)
        last = last->next;

    for (const Tac_StaticLocal *sl = fn->u.function.static_locals; sl; sl = sl->next) {
        Besm_Instr *items = static_data_items(sl->type, sl->init_list, true, dialect);
        Besm_Instr *site  = static_local_label_site(items);
        // A plain data word labels through `name`; a Z00 address word (pointer init) labels
        // through `label`, since its `name` already holds the referenced symbol.
        if (site && site->kind == BESM_DATA_Z00)
            site->label = xstrdup(sl->name);
        else if (site)
            site->name = xstrdup(sl->name);
        else
            // Unreachable: static_data_items always yields a labelable or Z00 first item.
            fatal_error("internal error: static local %s has no labelable init item", sl->name);
        insert_before_end(last, items);
    }
}

// Pack a string static-init into a chain of BESM_DATA_LOG words (6 KOI-7 bytes per
// word, big-endian).  When `label` is non-NULL it is set as the Madlen label of the
// first word.  Used both for char-array data and for string constants folded into a
// referencing module.
Besm_Instr *besm_string_log_items(const Tac_StaticInit *init, const char *label,
                                  Besm_Dialect dialect)
{
    if (init->kind != TAC_STATIC_INIT_STRING)
        fatal_error("string constant init is not a string");

    size_t raw_len, len;
    const char *raw = string_init_bytes(init, &raw_len);
    char *enc       = encode_static_string(raw, raw_len, &len, dialect);
    const char *s   = enc;
    size_t nbytes   = len + (init->u.string.null_terminated ? 1 : 0);
    if (nbytes == 0)
        nbytes = 1;

    Besm_Instr *head = NULL, **tail = &head;
    for (size_t w = 0; w * 6 < nbytes; w++) {
        uint64_t word = 0;
        for (int b = 0; b < 6; b++) {
            size_t pos      = w * 6 + b;
            unsigned char c = (pos < len) ? (unsigned char)s[pos] : 0;
            word            = (word << 8) | c;
        }
        Besm_Instr *si = besm_new_instr(BESM_DATA_LOG);
        si->log_val    = word;
        if (w == 0 && label)
            si->name = xstrdup(label);
        *tail = si;
        tail  = &si->next;
    }
    xfree(enc);
    return head;
}

// Find the init data of the TAC_TOPLEVEL_STATIC_CONSTANT named `name`, or NULL.
static const Tac_StaticInit *find_string_constant(const Tac_TopLevel *program, const char *name)
{
    for (const Tac_TopLevel *tl = program; tl; tl = tl->next)
        if (tl->kind == TAC_TOPLEVEL_STATIC_CONSTANT && tl->u.static_constant.name &&
            strcmp(tl->u.static_constant.name, name) == 0)
            return tl->u.static_constant.init;
    return NULL;
}

// Does any instruction in `list` carry `name` (as an operand or label)?
static bool instr_list_references(const Besm_Instr *list, const char *name)
{
    for (; list; list = list->next)
        if (list->name && strcmp(list->name, name) == 0)
            return true;
    return false;
}

static bool module_references(const Besm_Module *module, const char *name)
{
    for (const Besm_Func *fn = module->funcs; fn; fn = fn->next)
        for (const Besm_Block *b = fn->blocks; b; b = b->next)
            if (instr_list_references(b->body, name))
                return true;
    for (const Besm_DataSection *s = module->sections; s; s = s->next)
        if (instr_list_references(s->items, name))
            return true;
    return false;
}

// Unlink and free every BESM_STMT_SUBP whose name matches `name` from the list at *head.
static void remove_subp(Besm_Instr **head, const char *name)
{
    Besm_Instr *prev = NULL;
    Besm_Instr *i    = *head;
    while (i) {
        if (i->kind == BESM_STMT_SUBP && i->name && strcmp(i->name, name) == 0) {
            Besm_Instr *next = i->next;
            if (prev)
                prev->next = next;
            else
                *head = next;
            i->next = NULL;
            besm_free_instr(i);
            i = next;
        } else {
            prev = i;
            i    = i->next;
        }
    }
}

// Splice `chain` into block->body immediately before the BESM_STMT_END terminator.
static void insert_before_end(Besm_Block *block, Besm_Instr *chain)
{
    if (!chain)
        return;
    Besm_Instr *ctail = chain;
    while (ctail->next)
        ctail = ctail->next;

    Besm_Instr *prev = NULL;
    for (Besm_Instr *i = block->body; i; prev = i, i = i->next) {
        if (i->kind == BESM_STMT_END) {
            ctail->next = i;
            if (prev)
                prev->next = chain;
            else
                block->body = chain;
            return;
        }
    }
    // No END (should not happen for a well-formed function): append at the tail.
    if (prev)
        prev->next = chain;
    else
        block->body = chain;
}

//
// Fold every string constant the module references into the module itself as a local
// label, instead of emitting it as a separate global `,name,` module.  The per-unit
// `_strN` name is module-local here, so it can no longer collide across separately
// assembled objects.  For each referenced constant: drop its external SUBP, then
// append its packed data words (labeled with the constant name) — before the function
// `,end,` for a code module, or at the tail of the data section for a data module.
//
void besm_fold_string_constants(Besm_Module *module, const Tac_TopLevel *program,
                                Besm_Dialect dialect)
{
    for (const Tac_TopLevel *tl = program; tl; tl = tl->next) {
        if (tl->kind != TAC_TOPLEVEL_STATIC_CONSTANT || !tl->u.static_constant.name)
            continue;
        const char *name = tl->u.static_constant.name;
        if (!module_references(module, name))
            continue;

        for (Besm_Func *fn = module->funcs; fn; fn = fn->next)
            for (Besm_Block *b = fn->blocks; b; b = b->next)
                remove_subp(&b->body, name);
        for (Besm_DataSection *s = module->sections; s; s = s->next)
            remove_subp(&s->items, name);

        const Tac_StaticInit *init = find_string_constant(program, name);
        Besm_Instr *chain          = besm_string_log_items(init, name, dialect);

        if (module->funcs) {
            Besm_Block *last = module->funcs->blocks;
            while (last->next)
                last = last->next;
            insert_before_end(last, chain);
        } else if (module->sections) {
            Besm_DataSection *last = module->sections;
            while (last->next)
                last = last->next;
            Besm_Instr **t = &last->items;
            while (*t)
                t = &(*t)->next;
            *t = chain;
        }
    }
}
