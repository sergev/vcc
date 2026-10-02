# Madlen: The BESM-6 Autocode Assembler

*(Based on Chapter III of Saltykov & Makarenko, "Programming in FORTRAN", 1975.)*

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Statement Syntax](#2-statement-syntax)
3. [Identifiers](#3-identifiers)
4. [Mnemonics](#4-mnemonics)
5. [Full Address](#5-full-address)
6. [Index Register Selector](#6-index-register-selector)
7. [Labels](#7-labels)
8. [Constants](#8-constants)
9. [Literal Addresses](#9-literal-addresses)
10. [Declarations](#10-declarations)
11. [Parametric Commands](#11-parametric-commands)
12. [Data and Distribution](#12-data-and-distribution)
13. [Comments](#13-comments)
14. [Subprogram Design Rules](#14-subprogram-design-rules)
15. [Addressing and Basing](#15-addressing-and-basing)
16. [Complete Examples](#16-complete-examples)
17. [Standard Module Output Format](#17-standard-module-output-format)
18. [Error Diagnostics](#18-error-diagnostics)
19. [Control Cards, Editing, and Service](#19-control-cards-editing-and-service)
20. [Tips and Recommendations](#20-tips-and-recommendations)
21. [Programming Techniques](#21-programming-techniques)
- [Conclusion](#conclusion)

---

## 1. Introduction

Madlen is the symbolic assembly language — called *autocode* in Soviet computing literature — for the BESM-6 computer operating under the Dubna monitor system. It serves two distinct but closely related roles:

1. **A standalone low-level programming language.** Madlen gives the programmer direct, transparent access to every feature of the BESM-6 instruction set, including its unusual floating-point word format, the AU mode register, the stack pointer convention, and all arithmetic, logical, and branch operations.

2. **An intermediate language for compiler output.** The FORTRAN compiler for the Dubna system translates source programs into Madlen, then assembles the Madlen output into a binary load module. Because of this dual role, every non-executable (declarative) FORTRAN statement has an exact Madlen counterpart: COMMON blocks, EQUIVALENCE, DATA, EXTERNAL, ENTRY, and so on.

(Note: ALGOL-GDR procedures are not translated through Madlen. However, a reverse-translation utility can produce Madlen-like listings of assembled ALGOL procedures for inspection.)

Madlen is one of three BESM-6 assemblers documented here. The others are
[Bemsh](Bemsh.md) — the older Cyrillic-mnemonic autocode (Shtarkman, 1967), whose surface form
differs substantially (see [Bemsh §"How Bemsh differs from Madlen"](Bemsh.md)) — and the
modern Unix assembler [`b6as`](Besm6_Unix_Assembler.md), which keeps Madlen's mnemonics but
uses AT&T-style syntax. This BESM-6 backend emits Madlen.

### Relationship Between Autocode and Machine Instructions

An autocode *statement* is a symbolic notation for a machine instruction or assembler directive. The fundamental rule is:

> **Each autocode command statement translates to exactly one machine instruction.**

The exceptions to this one-to-one rule are limited to a few situations arising from the basing mechanism (see [Section 15](#15-addressing-and-basing)), where a single autocode statement with a non-empty index register and an internal address is translated into two machine instructions.

### Translator Capabilities

The Madlen translator provides:

- **Diagnostic messages** for syntactic and semantic errors, printed immediately before the offending statement.
- An **editing apparatus** that formats autocode listings independently of how the source was keypunched: delimiters are placed at fixed columns, labels and addresses are left-aligned, index register selectors are right-aligned.
- **High translation speed**: approximately 200 statements per second.
- An optional **standard module listing** printed to the left of the autocode text, showing the binary load module in octal.

A Madlen subprogram written according to the conventions in [Section 14](#14-subprogram-design-rules) is interoperable with FORTRAN and ALGOL subprograms and can be deposited in the general-purpose program library.

### Architecture Background

The BESM-6 is a 48-bit word machine with 32,768 words of address space (addresses 00000–077777 octal). Instructions are 24 bits wide and are packed two per word: the *left* instruction occupies bits 48–25 and the *right* instruction occupies bits 24–1. Branches always target word boundaries (the left instruction of a word).

The machine registers most relevant to Madlen programming are:

| Register | Width | Description |
|----------|-------|-------------|
| A | 48 bits | Accumulator — floating-point working register |
| Y | 48 bits | Younger-bits register (RMR) — extension of A for double precision and logical operations |
| R | 6 bits | AU mode register — controls arithmetic behavior and branch conditions |
| M[1]–M[15] | 15 bits each | Index (modifier) registers; M[15] = stack pointer by convention |
| C | 15 bits | Address-modification register — added to effective address; reset after every instruction except UTC and WTC |
| K | 15 bits | Program counter |

**M[0]** always reads as zero.

For a complete description of the BESM-6 instruction set, including instruction encoding, floating-point format, and the ω condition system, see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md).

---

## 2. Statement Syntax

A Madlen subprogram consists of a sequence of *statements*, each occupying one punched card. Some statements with long address fields may be continued onto subsequent cards with the CONT mnemonic.

### General Format

The general format of an autocode statement is:

```
<label> : <index-reg> , <mnemonic> , <full-address>
```

The field layout corresponds directly to the fields of a machine instruction:

| Field | Position | Required? | Description |
|-------|----------|-----------|-------------|
| label | Before `:` | Optional | Symbolic name for this statement's address |
| index-reg | Between `:` and first `,` | Optional | Index register selector (0–15) |
| mnemonic | Between the two `,` | **Mandatory** | Operation or directive code |
| full-address | After second `,` | Optional | Address or constant specification |

**The two commas are mandatory** and serve as the primary delimiters between fields. The colon separates the label from the index register. The mnemonic is always present (with one obscure exception in the SET directive, described in [Section 12](#12-data-and-distribution)).

Any statement may be placed in columns 2–42 of a punched card. Long statements (ISO, BLOCK, CONT, and others) may extend to column 72. Columns 73–80 always belong to the comment field.

### Examples

The following examples illustrate the variety of statement forms:

```
    *5 : 14 ,XTA,
```
Label `*5`, index register 14, mnemonic `XTA`, empty address.

```
   ABT :    ,A+X, INT
```
Label `ABT`, no index register, mnemonic `A+X`, address `INT`.

```
    /7 :    ,BSS,
```
Label `/7`, mnemonic `BSS` (block storage reservation), empty address.

```
       :    ,XTA, A+4
```
Empty label (forces left-half placement), mnemonic `XTA`, address `A+4`.

```
            ,A*X, =R5.
```
No label, no index register, mnemonic `A*X`, literal address `=R5.` (the real constant 5.0).

```
 PRINT8:    ,NAME,
```
Label `PRINT8`, mnemonic `NAME` (subprogram header).

---

## 3. Identifiers

An *identifier* is a symbolic name used to refer to an address, a constant, or an index register number.

### Definition

**An identifier is a sequence of letters and digits that begins with a letter.**

In Madlen, "letters" include all characters from both the Latin and Cyrillic alphabets, as well as the two special characters `*` (asterisk) and `/` (slash). This is broader than FORTRAN or ALGOL, where identifiers are restricted to Latin letters and digits.

The **maximum length** of an identifier is 8 characters. Spaces within an identifier are ignored during parsing, so `A B C` and `ABC` denote the same identifier.

### The 8-Character Limit Is Significant — Truncation Collisions

This 8-character limit is a **hard, unavoidable constraint** of the Madlen assembler and the
Dubna monitor's loader/linker, not a compiler choice. Characters past the eighth are simply
**dropped**: only the first 8 characters distinguish one name from another. Two distinct names
that share the same first 8 characters become the **same identifier** after truncation, and the
assembler/loader silently merges them — there is no diagnostic. When two such names are both
*defined* (e.g. two `,name,` subprograms), the later definition wins and every call to either C
name resolves to that one body.

The BESM-6 backend maps C identifiers to Madlen identifiers in `sanitize_name`
([backend/besm6/emit_madlen.c](emit_madlen.c)): it replaces `_`→`*`, `$`→`/`,
`%`→`*`, and **truncates to 8 characters**. Consequently, two C names that agree in their first
8 significant characters collide. For example:

```
get_multidim_ptr_diff      →  get*mult
get_multidim_ptr_diff_2     →  get*mult     (same!)
```

Both functions emit a `get*mult:` label; the loader keeps one, so `get_multidim_ptr_diff(...)`
actually runs the body of `get_multidim_ptr_diff_2` (or vice-versa). The symptom is a wrong
runtime result with **no compile- or link-time error**.

**Mitigation:** keep externally-visible C identifiers (functions, file-scope globals, anything
that becomes a Madlen label or `,subp,`/`,call,` operand) **distinct within their first 8
characters** after the `_`/`$`/`%` substitution. Block-scope `%`-locals are frame slots, not
labels, so they are unaffected. There is no general fix in the toolchain — the limit is imposed
by Madlen and Dubna themselves; a future name-mangling scheme (hashing the tail into the 8-char
budget) could reduce, but not eliminate, the risk.

### Special Identifier `*`

The identifier consisting of a single asterisk, `*`, has a special meaning: **it denotes the address of the instruction in which it appears**. This is the autocode equivalent of the "current location counter". For example:

```
    ,UJ, *+2
```
means "unconditional branch to the left instruction of the word whose address is two words beyond the current instruction." This is the standard Madlen idiom for a short forward jump over one machine word.

### Examples of Valid Identifiers

```
PROGRAM
*15
/101
*AB*
TAB
```

### Alphabet Outside Identifiers

Characters that are part of the Madlen alphabet but do not appear inside identifiers:

```
+   plus
-   minus
(   left parenthesis
)   right parenthesis
,   comma
.   period
=   equals sign
:   colon
'   apostrophe
```

In text constants and comments, the following additional characters are permitted:

```
;   semicolon
<   less-than
>   greater-than
[   left bracket
]   right bracket
_   underscore
!   exclamation mark
$   dollar sign  (or  ◇  lozenge)
```

---

## 4. Mnemonics

The mnemonic is the most important field of an autocode statement — it determines the operation being performed and governs the interpretation of all other fields.

**A mnemonic is a specific sequence of letters (in the Madlen sense), digits, and the signs `+` and `−`.**

### Command Mnemonics

Command mnemonics are chosen so that their structure directly communicates what the corresponding machine instruction does. They normally consist of **three characters** and are constructed according to a systematic naming convention.

#### Second Character: Operation Type

The second character of a command mnemonic identifies the class of operation:

| Second char | Meaning |
|-------------|---------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
| `T` | Transfer (move data) |
| `Z` | Zero-jump (conditional branch on ω = 0) |
| `J` | Jump (unconditional branch) |
| `S` | Shift |
| `A` | AND (logical multiply) |
| `O` | OR (logical add) |
| `E` | XOR (exclusive-or / comparison) |
| `R` | Round / cyclical add |
| `L` | Loop (end-of-loop test) |

#### First and Third Characters: Register or Address Type

The first and third characters indicate the source/destination register or the type of address operand:

| Character | Meaning |
|-----------|---------|
| `A` | Accumulator |
| `S` | Stack (magazine) — M[15] |
| `Y` | Younger-bits register (RMR) |
| `M` | Modifier register pointer (index register number) |
| `E` | Exponent "register" — bits 48–42 of the accumulator |
| `X` | Short address (memory, 12-bit offset, Format 1) |
| `V` | Long address, not modified by index register (Format 2) |
| `U` | Long address, modified by index register (Format 2) |
| `N` | Address not referring to a memory cell, interpreted mod 2⁷ |

When the second character is `T` (transfer), the **third** character indicates the destination and the **first** character indicates the source.

#### Examples

| Mnemonic | Explanation |
|----------|-------------|
| `ATX` | Accumulator → memory (short address): store A |
| `XTA` | Memory (short address) → Accumulator: load A |
| `ASN` | Accumulator Shift by N: shift by the low 7 bits of the effective address |
| `UZA` | Unconditional Zero-branch by Address (long, index-modified): branch if ω = 0 |
| `VTM` | long address → Modifier register: load index register immediate |
| `E+N` | Exponent plus N: add the low 7 bits of EA minus 64 to A's exponent |
| `A*X` | Accumulator × memory (short address): floating-point multiply |
| `A+X` | Accumulator + memory: floating-point add |
| `VJM` | Jump to subroutine (long, non-indexed address) |
| `VLM` | Loop (long address) |

### Special Mnemonics

Certain mnemonics fall outside the three-character naming convention. These include:

- **CALL** — call an external subprogram
- **BASE**, **BAS** — set base register for address basing
- **BSS** — block storage (reserve memory)
- **EQU** — equivalence (assign a value to an identifier)
- **NAME**, **END**, **ENTRY**, **SUBP**, **BLOCK** — structural directives
- **OCT**, **LOG**, **INT**, **REAL**, **ISO**, **GOST**, **TEXT**, **TEL** — constant declarations
- **DATA**, **SET** — data distribution
- **REL**, **RELS** — relocatable basing

### Octal Machine Codes

Instead of symbolic mnemonics, a programmer may write the raw octal machine code for any instruction. However, this practice is strongly discouraged: it sacrifices readability and, more importantly, disqualifies those instructions from the automatic basing mechanism (see [Section 15](#15-addressing-and-basing)).

---

## 5. Full Address

The *full address* (Russian: *полный адрес*) is the address expression that appears in the address field of an autocode statement.

### Elementary Address Forms

The following elementary forms are permitted in an address field:

1. **Identifier** — a symbolic name defined elsewhere in the subprogram.
2. **Unsigned decimal integer** — a literal decimal constant.
3. **Octal number followed by `B`** — e.g., `177B` denotes the octal value 177.
4. **`*`** — the address of the current instruction.
5. **Literal address** — a constant embedded directly in the address field (see [Section 9](#9-literal-addresses)).

### Composite Full Address

A *full address* is any address expression consisting of **at most two** elementary forms (types 1–5 above), optionally combined with the `+` or `−` operators. The address field may also be **empty**, which corresponds to the value zero.

Examples:
```
A
*+2
X+5
C+30B
-AB+C15
```

The first example uses a single identifier. The second adds the constant 2 to the current address. The third adds the identifier `X` and the decimal constant 5. The fourth adds the identifier `C` and the octal value 30. The fifth subtracts the identifier `AB` from the identifier `C15`.

### Address Arithmetic

Addresses written as pure decimal or octal numbers (or arithmetic combinations thereof) are **absolute** — their values are independent of where the subprogram is loaded in memory.

Addresses involving identifiers are **relative** — their actual values are determined by the loader when the subprogram is placed in memory.

All address arithmetic is performed **modulo 2¹⁵**: only the low 15 bits of the computed value are used. Negative values use **two's-complement representation**; for instance, the address −1 is represented as octal 77777В.

---

## 6. Index Register Selector

The *index register selector* specifies which of the 15 general-purpose index registers M[1]–M[15] is used to modify the instruction's effective address.

### Numeric Form

The selector is a decimal number in the range **1–15**. A selector of **0** or an empty selector means no index register modification is applied.

In the machine instruction word, the selector occupies bits 24–21 (the M field) in both Format 1 and Format 2 instructions. See [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) for the detailed bit layout.

### Symbolic Form

For readability and ease of changing register assignments, identifiers may be used as index register selectors. In this case, the identifier must be defined (via an EQU statement) to have the numeric value of the desired register number.

Example:
```
    J : ,EQU, 8
    ...
    J ,XTA, A   ; load memory[A + M[8]] into accumulator
```

### Zero vs. Empty Selector

Note that in some contexts (specifically the basing mechanism described in [Section 15](#15-addressing-and-basing)), the translator treats an **empty** selector differently from an explicit **0** selector. An empty selector means "no register, and this instruction is eligible for automatic basing." An explicit `0` is treated as a non-empty selector for basing purposes.

---

## 7. Labels

A *label* is an identifier written before the colon in an autocode statement. Labels serve three related purposes:

1. **Definition**: A label *defines* the identifier, assigning it the address of the labeled statement. This is the primary mechanism for declaring constants, variables, and code addresses in Madlen. Each identifier may appear as a label at most once.

2. **Left-half placement**: When a label appears before a command, the translator places that command in the **left half** of a machine word (bits 48–25). Recall that the BESM-6 packs two 24-bit instructions per 48-bit word, and branches always target the left half.

3. **Internal address marker**: The address recorded for a label (before a command, constant, or BSS directive) is an *internal address* of the subprogram — one that can be automatically adjusted by the basing mechanism (see [Section 15](#15-addressing-and-basing)).

### Empty Label

A bare colon `:` (without a preceding identifier) is called an *empty label*. It forces the following command into the left half of a machine word, without defining any identifier. If the previous machine word was only partially filled (containing only a left instruction), the translator automatically pads it with a right instruction of the form `00 22 00000` (a UTC with address 0, which is a no-op).

### Automatic Left-Half Placement

The translator automatically places certain commands in the left half of a machine word without requiring an explicit label or colon. The following mnemonics always imply a labeled position:

```
LIB,  *60,  *66
```

Additionally, commands that **immediately follow** any of these mnemonics are automatically placed in the left half:

```
VJM, FUN, PRINT, TAPE, DRUM, SJ, CTX,
*50 – *57, *61 – *65, *67, *70 – *77
```

**Important:** This automatic labeling behavior applies only to symbolic mnemonics, not to their raw octal equivalents.

### Label Placement Tips

Experienced Madlen programmers often use an *empty BSS* (a BSS directive with an empty address) as a flexible label:

```
    A :    ,BSS,
    *3 : 14 ,XTA,
```

Here both `A` and `*3` label the same word. Because the BSS with empty address reserves no memory, labels can be moved up or down simply by inserting or removing cards, without breaking the subprogram's structure. This practice is the Madlen equivalent of FORTRAN's CONTINUE statement.

---

## 8. Constants

Madlen provides four families of constants: octal, integer, real, and text. Constants may be introduced either as standalone statements (using the constant-type mnemonic) or inline as *literal addresses* in command statements (see [Section 9](#9-literal-addresses)).

The index register selector in a constant statement is ignored by the translator (it may be used for programmer-defined numbering). The label has the same meaning as for commands.

Constants always occupy a whole number of machine words; for the first three types (OCT, LOG, INT) this is exactly one word.

---

### 8.1 OCT — Octal Constant

**Syntax:**
```
    <label> : ,OCT, <octal-digits>
```

The address field contains a sequence of **up to 16 octal digits** (without a trailing `B`). The translator packs these digits into the machine word from **left to right, starting from the most-significant bits**. If fewer than 16 digits are given, zeros are appended on the **right**. If more than 16 digits are given, the excess rightmost digits are discarded.

**Examples:**

```
    ,OCT, 4
```
Translates to: `4000 0000 0000 0000` (the single digit `4` in bits 48–45, zeros in bits 44–1).

```
    ,OCT, 1777 7356 4216 3730 157
```
Translates to: `1777 7356 4216 3730` (the rightmost three digits `157` are discarded because only 16 digits fit).

### 8.2 LOG — Logical Octal Constant

**Syntax:**
```
    <label> : ,LOG, <octal-digits>
```

LOG constants are identical to OCT constants in every respect except one: the digits are packed from **right to left, starting from the least-significant bits**, and zeros are padded on the **left**.

LOG constants are the Madlen equivalent of FORTRAN's octal constants (which also fill from the right).

**Examples:**

```
    ,LOG, 4
```
Translates to: `0000 0000 0000 0004` (the digit `4` in bits 3–1, zeros in bits 48–4).

```
    ,LOG, 1777 7356 4216 3730 157
```
Translates to: `1777 7356 4216 3730` — the same result as the OCT example above, because 16 digits fully fill the word in both directions.

### 8.3 INT — Integer Constant

**Syntax:**
```
    <label> : ,INT, <decimal-integer>
```

The address field contains any decimal integer, either unsigned or with a leading `−` sign. The value must not exceed 2⁴⁰ − 1 in absolute value.

The translator produces a **non-normalized floating-point word** with the exponent field set to 40 and with the mantissa holding the two's-complement representation of the integer. Because the exponent field records the binary position of the decimal point at the very end of the mantissa (position 0, i.e., integer mode), the word is an exact integer representation.

- For positive values, the mantissa occupies bits 40–1 (the sign bit 41 is 0).
- For negative values, the mantissa is the 40-bit two's complement, with bit 41 set to 1.

The exponent of 40 in biased form is 40 + 64 = 104 decimal = 150 octal; packed into the 7-bit exponent field (bits 48–42) this gives the pattern `1 000 000` in binary = `100` octal for bits 48–45, for a value of `6400 0000 0000 0000` when the mantissa is zero. (Each group of 3 bits maps to one octal digit.)

**Examples:**

```
    ,INT, 38
```
Decimal 38 = octal 46. The machine word is `6400 0000 0000 0046`. Bits 48–42 hold exponent = 104 (decimal), bits 41–1 hold `0 000 000 000 000 046₈`.

```
    ,INT, -5
```
Negative 5 in two's-complement (40-bit) with the sign in bit 41:
Machine word = `6437 7777 7777 7773`.

INT constants are identical in format to FORTRAN integer constants.

**Note on precision:** The autocode and FORTRAN translators may use different decimal-to-binary conversion routines. For constants that cannot be represented exactly, the least-significant three bits of the mantissa may differ between the two translators.

### 8.4 REAL — Floating-Point Constant

**Syntax:**
```
    <label> : ,REAL, <decimal-number>
```

The address field holds a decimal floating-point number. The rules are analogous to FORTRAN: a decimal point is mandatory, and an exponent may be specified with `E` followed by a signed integer.

The BESM-6 floating-point format stores the value `(0.mantissa − sign) × 2^(exponent − 64)` where:
- Bits 48–42 (7 bits): exponent, biased by 64.
- Bit 41: sign (0 = positive, 1 = negative).
- Bits 40–1 (40 bits): mantissa in two's complement.

A normalized number has bits 41 and 40 differing (the top mantissa bit disagrees with the sign bit). Magnitudes lie in [0.5, 1.0) for positive and (−1.0, −0.5] for negative normalized values.

**Examples:**

```
    ,REAL, 1.
```
The value 1.0 is `0.1₂ × 2¹`, so exponent = 1 + 64 = 65 = `1 000 001₂`; mantissa `01₂` in bits 41–40. The full bit pattern in bits 48–40 is `1 000 001 01₂` = `405₈`. Machine word: `4050 0000 0000 0000`.

```
    ,REAL, 2.E1
```
Value 20.0 = `0.101₂ × 2⁵`; exponent = 5 + 64 = 69 = `1 000 101₂`, mantissa = `01010₂` in bits 41–37. Bits 48–37: `1 000 101 01010₂` = `4252₈`. Machine word: `4252 0000 0000 0000`.

```
    ,REAL, -1.
```
Value −1.0: sign bit 41 = 1, mantissa = `10₂` (two's complement of +0.1₂ with bit 41 as sign). Machine word: `4020 0000 0000 0000`.

REAL constants are identical in format to FORTRAN real constants. However, due to different decimal-to-binary converters, results for non-exactly-representable values may differ in the last three bits of the mantissa compared to FORTRAN output.

---

### 8.5 Text Constants

Madlen supports four text constant types with different character encodings and packing densities.

#### ISO — 8-Bit ISO Text

**Syntax:**
```
    <label> : ,ISO, nH<characters>
```

where `n` is the character count and the `H` is a Hollerith indicator inherited from FORTRAN notation. The constant may contain up to **128 characters** packed **6 characters per machine word** (8 bits per character).

Characters may also be specified by their octal ISO code, enclosed in apostrophes:

```
    ,ISO, 6HB'105''123'M-6
```

Here `'105'` is the ISO code for `E` (octal 105) and `'123'` is the ISO code for `S` (octal 123). This mechanism allows any 8-bit pattern, not just printable characters, to be embedded in an ISO constant.

Special case: the apostrophe character itself must be specified in octal as `'47'`.

If `n = 0` or is omitted, the translator assumes `n = 6` and generates one machine word of six spaces.

If `n` is not a multiple of 6, the translator appends enough spaces to complete the final word (unless the next statement is a CONT continuation).

The label on an ISO or CONT statement corresponds to the word containing the *first character of that statement's address field* — not necessarily the first character of the entire constant.

**Multi-word text example using CONT:**
```
        ,ISO, 9HBESM-6 JI
     T: ,CONT, 2HNR
        ,CONT, 6H DUBNA
```
This produces 3 machine words containing the 18-character string `BESM-6 JINR DUBNA` (padded with one space at the end). Label `T` refers to the second word (beginning with `N`).

ISO constants are equivalent to FORTRAN Hollerith constants.

#### GOST — GOST Printer Encoding

**Syntax:**
```
    <label> : ,GOST, nH<characters>
```

GOST constants are identical to ISO constants in every structural respect, but characters are encoded according to the **АЦПУ-128** character set (the Soviet standard encoding for line-printer output). This encoding is required when text is sent directly to the printer via the print extracode, bypassing FORTRAN's I/O formatting. For ordinary text output through FORTRAN, ISO encoding is used instead.

#### TEXT — 6-Bit Internal Encoding

**Syntax:**
```
    <label> : ,TEXT, nH<characters>
```

TEXT constants use a **6-bit** internal encoding, packing **8 characters per machine word** (instead of 6 as in ISO). This encoding is used for the internal representation of text within the Dubna system — in particular, all subprogram names in library catalogs are stored in TEXT encoding. Programs that work with library catalogs frequently need TEXT constants.

#### TEL — 5-Bit Teleprinter Encoding

**Syntax:**
```
    <label> : ,TEL, nH<characters>
```

TEL constants use a 5-bit teleprinter (telegraph) encoding. For packing purposes, each 5-bit code is zero-padded to 8 bits (two zeros on the left, one zero on the right), and then packed 6 characters per machine word. The translator can automatically insert teleprinter register-shift codes where needed. TEL constants are used almost exclusively in system programs.

---

## 9. Literal Addresses

A *literal address* is a constant value written directly in the address field of a command statement, instead of naming a separate constant location.

### Syntax

A literal address is recognized by a leading `=` sign:

| Form | Type produced |
|------|--------------|
| `= <octal-number>` | LOG constant (right-fill octal) |
| `=: <octal-number>` | OCT constant (left-fill octal) |
| `=I <decimal-integer>` | INT constant |
| `=R <decimal-real>` | REAL constant |
| `=nH <characters>` | ISO text constant (n ≤ 6) |

The `=` prefix signals that the translator should generate a constant automatically, reserve one machine word for it at the end of the subprogram, and replace every occurrence of the literal address with the address of that reserved word.

The restriction `n ≤ 6` for ISO literals follows from the fact that each command can work with only one machine word.

### Deduplication

Within the same literal-address class (same prefix), if two literal addresses translate to the same machine word, they share a single reserved location. This deduplication does not extend across classes: `=R1.` and `=:4050` may both translate to `4050 0000 0000 0000`, but they are considered different classes and each reserves its own word.

**Example demonstrating deduplication:**
```
    ,XTA, =R1.
    ,A*X, =R.99999 99999 999
    ,A/X, =:4050
    ,A+X, =6H'202''200''0''0''0''0'
```
All four constants translate to the same machine word `4050 0000 0000 0000`. However, only the first two (both class `=R`) are treated as equivalent and share one word. The third (class `=:`) and the fourth (class `=nH`) each reserve their own separate word. Total: 3 words allocated, all containing `4050 0000 0000 0000`.

### Examples

```
    10 ,XTA, =-77        ; load LOG octal constant 77 (= 0...077₈)
       ,A*X, =R .5E6     ; multiply by 500000.0
       ,AAX, =:774       ; AND with OCT 774 (= 000...774₈ left-filled)
       ,XTS, =3HABC      ; push A, load ISO string "ABC"
       ,XTA, =I2         ; load integer constant 2
    14 ,VTM, =R1.        ; set M[14] ← address of constant 1.0
```

Literal addresses make programs more readable by keeping the constant value visible at the point of use, at the cost of giving up control over the ordering of the generated constants.

---

## 10. Declarations

Identifiers that appear in the index register selector or in the address field of command statements — and that are not themselves labels — must be *declared* before use. Declarations map identifiers to numeric values (memory addresses, register numbers, etc.).

### 10.1 BSS — Block Storage

**Syntax:**
```
    <label> : ,BSS, <full-address>
```

BSS reserves a contiguous block of memory *inside* the subprogram. The size of the block is given by the full address field.

**Examples:**
```
    A   : ,BSS, 1       ; reserve 1 word; A = address of that word
    *C  : ,BSS,         ; reserve 0 words (empty BSS — pure label)
    TAB : ,BSS, 100     ; reserve 100 words; TAB = address of first word
    T   : ,BSS, 25B     ; reserve 25₈ = 21₁₀ words
    Z   : ,BSS, T - A   ; reserve T−A words
```

The empty BSS (`*C` in the above) is a particularly important idiom: it reserves no memory and simply attaches a label to the next assembled word. This provides a flexible label that can be moved without restructuring the code.

Because BSS allocates space within the subprogram, addresses defined by BSS labels are *internal* addresses and are eligible for automatic basing (see [Section 15](#15-addressing-and-basing)).

The TAB example above declares an array of 100 words. The programmer may then use addresses of the form `TAB+n` (for 0 ≤ n ≤ 99) to access individual elements.

BSS is the Madlen equivalent of FORTRAN's CONTINUE (when empty) and of variable/array declarations (when non-empty).

### 10.2 EQU — Equivalence

**Syntax:**
```
    <identifier> : ,EQU, <full-address>
```

EQU assigns an identifier to the value of a full address expression, without reserving any memory. EQU is used when:
- An identifier should stand for an index register number.
- An identifier should be an alias for an address that is already reserved elsewhere.
- An identifier should denote a computed constant (e.g., array offsets).

**Examples:**
```
    A : ,EQU, 17B       ; A ← octal 17 (e.g., index register 15)
    C : ,EQU, A + 3     ; C ← value of A plus 3
    L : ,EQU, * + 5     ; L ← address of current location plus 5
    T : ,EQU, =R1       ; T ← address of the literal constant 1.0
```

**Placement rule**: At the point where an identifier appears in the address field of an EQU, it must already be defined (through a prior label, another EQU, etc.). EQU statements are typically placed near the beginning of the subprogram, in dependency order.

EQU is the Madlen counterpart of FORTRAN's EQUIVALENCE statement.

Identifiers that represent index register numbers or N-type addresses (see [Section 4](#4-mnemonics)) can *only* be defined via EQU (or its variants below) — they cannot appear as labels on memory locations.

### 10.3 NAME — Subprogram Header

**Syntax (three forms):**
```
    <identifier> :       ,NAME,
    <identifier> : <reg> ,NAME,
    <identifier> : <reg> ,NAME, ***
```

NAME is mandatory and must be the first substantive statement of every Madlen subprogram. It defines the subprogram's entry-point name.

- **Form 1** (no index register): standard header; no automatic global basing.
- **Form 2** (with register number `reg`): requests global basing using three index registers starting at `reg` (see [Section 15.3](#153-global-basing-three-base-registers)).
- **Form 3** (with `***` in address): like Form 2 but suppresses the automatic expansion of indexed short-address commands into two-instruction sequences.

### 10.4 END — End of Subprogram

**Syntax:**
```
    ,END,
```

END marks the physical end of the subprogram source. The translator finalizes all pending tables (literals, descriptions) when it encounters END. Every subprogram must end with END.

END is the Madlen equivalent of FORTRAN's END statement.

### 10.5 CALL — External Subprogram Call

**Syntax:**
```
    ,CALL, <subprogram-identifier>
```

CALL invokes an external subprogram by name. The translator automatically:
1. Saves the return address in index register M[13].
2. Jumps to the named subprogram.
3. Places the instruction after CALL in the left half of the next machine word (so that the return target is at a word boundary).

CALL does not require a separate declaration of the called subprogram (unlike VJM). An alternative, non-standard calling sequence using VJM with M[13] is described in [Section 10.7](#107-subp--external-subprogram-declaration).

### 10.6 BLOCK — External Arrays and COMMON Blocks

**Syntax:**
```
    <block-id> : <char><type> ,BLOCK, <array-list>
```

BLOCK declares a group of arrays that live *outside* the subprogram's private address space — either in shared memory (analogous to FORTRAN COMMON blocks) or at fixed absolute addresses.

#### Placement Characteristic

The first character of the selector field specifies memory alignment:

| Char | Requirement |
|------|-------------|
| `L` | Arbitrary start address |
| `P` | Page-aligned: start address must be a multiple of 1024₁₀ |
| `S` | Sector-aligned: start address must be a multiple of 256₁₀ |

#### Array Type

The second character specifies the sharing policy:

| Char | Meaning |
|------|---------|
| `P` | Private (собственный): accessible only within this subprogram |
| `U` | "Unused" — treated identically to P |
| `C` | Common (общий): accessible to any subprogram that declares it with the same block identifier |

#### Array List

The address field contains a comma-separated list of arrays. Each entry has the form `<name>(<length>)`:
- If the length is omitted (e.g., `CD`), the length is 1.
- If the parentheses are empty (e.g., `EF()`), the length is 0, so the array's start address coincides with the next array's start address.

The total memory reserved equals the sum of all array lengths. The block identifier's address equals the start of the first array.

**Examples:**

```
    A : LC ,BLOCK, B(3), D(5), CD, EF(), *SQ(12)
```
Declares a common block of 3 + 5 + 1 + 0 + 12 = 21 words starting at an arbitrary address. `A` = address of `B`; `D` starts 3 words in; `CD` starts 8 words in; `EF` and `*SQ` both start 9 words in; `*SQ` runs for 12 words to the end.

```
    *ABC* : LC ,BLOCK, A, B(7), C(18)
```
This is the Madlen equivalent of the FORTRAN statement:
```fortran
    COMMON /ABC/ A, B(7), C(18)
```
The block identifier `*ABC*` must be wrapped in asterisks for FORTRAN compatibility (see [Section 14.3](#143-naming-conventions)).

#### BLOCK with CONT Continuation

Long array lists may be continued across multiple cards:
```
    A : LC ,BLOCK, B(3)
           ,CONT , D(5), CD
           ,CONT , EF(), *SQ(12)
```

#### Alternative Using EQU Chain

An equivalent notation without BLOCK:
```
      A : ,LC , 21      ; reserve 21 words
      B : ,EQU, A
      D : ,EQU, B+3
     CD : ,EQU, D+5
     EF : ,EQU, CD+1
    *SQ : ,EQU, EF
```
This is more verbose but makes the offset of each sub-array explicit. The BLOCK form is preferred for readability.

#### BLOCK with Absolute Addresses

For system programs that need to reference fixed memory locations:

```
    *20 : B ,BLOCK, A(23), D(2)
```
Equivalent to:
```
    A : ,EQU, 20B
    D : ,EQU, A+23
```
Here `*20B` (octal 20) is the absolute start address.

```
    *25 :   ,BLOCK, A(15), B(6)
```
Equivalent to (using decimal 25):
```
    A : ,EQU, 25
    B : ,EQU, A+15
```

#### BLOCK as Equivalence Sequence

```
    M : I ,BLOCK, A, B(15), C(6)
```
Equivalent to:
```
    A : ,EQU, M
    B : ,EQU, A+1
    C : ,EQU, B+15
```
where `M` must already be defined. These special BLOCK forms reserve no memory.

### 10.7 SUBP — External Subprogram Declaration

**Syntax:**
```
    <identifier> : ,SUBP,
```

SUBP declares that an identifier refers to an externally defined subprogram. This declaration is required when the subprogram is called without using the standard CALL mnemonic — for instance, via `13,VJM,<name>` or when the subprogram's name is passed as an actual parameter (analogous to FORTRAN's EXTERNAL statement).

**Alternative calling sequence (using VJM):**
```
    13 ,VJM, <subprogram-name>
```
Or, to return to a specific labeled address:
```
        13 ,VTM, *10
           ,UJ , <subprogram-name>
    *10 :  ,BSS,
```

In either of these cases, the called subprogram's identifier must be declared with SUBP.

### 10.8 ENTRY — Additional Entry Point

**Syntax:**
```
    <identifier> : ,ENTRY,
```

ENTRY defines a secondary entry point within the subprogram, analogous to FORTRAN's ENTRY statement. Multiple entry points allow several logical subprograms to share a single set of constants and COMMON blocks.

Each entry is, from the caller's perspective, a fully independent subprogram: it may have a different argument list and a different type (SUBROUTINE vs. FUNCTION). The total number of entry points (including the primary entry defined by NAME) must not exceed **20**.

The command immediately following an ENTRY statement is automatically placed in the left half of a machine word.

For internal calling between entries within the same subprogram, use CALL or a BSS-labeled VJM.

### 10.9 WEQ — Indirect Equivalence

**Syntax:**
```
    <identifier> : ,WEQ, <full-address>
```

WEQ (*косвенная эквивалентность* — indirect equivalence) assigns to the identifier the value of **bits 15–1 of the memory word** at the given full address, evaluated **at load time** (not at assembly time).

This is useful for variable-length memory regions. For example, the size of a memory region can be stored in a COMMON block (set by a DATA statement in a FORTRAN PROGRAM unit), and WEQ can then read that size to determine where to place BSS storage.

### 10.10 P\*P — Address Product

**Syntax:**
```
    <identifier> : ,P*P, (<full-address>) (<full-address>)
```

Assigns to the identifier the **product** of the two parenthesized full addresses, modulo 2¹⁶. This is used to compute the total size of multi-dimensional arrays:

```
    TOTAL : ,P*P, (ROWS)(COLS)
    ARR   : ,BSS, TOTAL
```

Combined with WEQ, this allows declaring arrays with dimensions that are not known until load time.

### 10.11 P/P — Address Quotient

**Syntax:**
```
    <identifier> : ,P/P, (<full-address>) (<full-address>)
```

Assigns to the identifier the integer **quotient** (rounded toward zero) of the two full addresses. One application is computing a page or sector number from an address, which is required by the block I/O extracode `*70`.

---

## 11. Parametric Commands

A *parametric command* is a way to write an arbitrary Format 2 (long-address) machine instruction using an octal opcode:

**Syntax:**
```
    <index-reg> ,Z<octal-opcode>, <full-address>
```

where `<octal-opcode>` is a single octal number in the range 0–37₈.

The Format 2 instruction format (see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md)) uses a 4-bit opcode in bits 19–16 and a 15-bit address in bits 15–1. The traditional octal notation for Format 2 opcodes is 20–37 (the leading `1` from bit 20 is implicit). Parametric commands provide access to any Format 2 opcode.

**Example:**
```
    5 ,Z31, 06412B
```
Translates to the machine instruction: `05 31 06412`.
- Index register = 5 (written `05` in octal)
- Opcode = 31 (octal), i.e., Format 2 opcode VJM
- Address = 06412₈

Parametric commands are **never subjected to basing**, regardless of the basing mode in effect.

---

## 12. Data and Distribution

In certain programs, initial values must be placed into memory locations that are *not* internal addresses of the assembling subprogram — for instance, into COMMON block cells or into buffers allocated by the loader. Madlen provides a DATA/SET mechanism for this purpose.

### 12.1 DATA — Begin Data Block

**Syntax:**
```
    ,DATA,
```

The DATA statement marks the beginning of a block of initial values to be distributed. It must appear at the end of the subprogram body, after all executable commands. The DATA block may contain constants of any type (OCT, LOG, INT, REAL, ISO, GOST, TEXT, TEL).

**Important:** Data is **not loaded into memory** at its assembled address. The loader reads the data, copies it to the target addresses specified by SET statements, and then discards the original (so the data is available *only* at the distributed target addresses, not at its source address).

### 12.2 SET — Distribution Directive

**Syntax:**
```
    n1 ,SET, A1
    n2 ,   , A2
```

A SET pair distributes `n1` consecutive words beginning at address `A1` into `n2` consecutive blocks of `n1` words each, starting at address `A2`. In most cases `n2 = 1` (a single copy).

- `n1` and `n2` are unsigned decimal integers.
- `A1` and `A2` are full addresses.
- The second statement uses an empty mnemonic field (a single space between the commas).

**Full example:**
```
        ,DATA,
     A: ,REAL, 1.
        ,ISO , 14HAUTOCODE MADLEN
      4 ,SET , A
      1 ,    , TABLE
```

This distributes the 4 words starting at `A` (one REAL word + three ISO words) once, placing them at addresses TABLE, TABLE+1, TABLE+2, TABLE+3. After loading, those words can be accessed as `TABLE`, `TABLE+1`, etc. The original addresses starting at `A` are no longer valid.

The DATA/SET mechanism implements the same functionality as FORTRAN's DATA statement.

---

## 13. Comments

Madlen provides three comment mechanisms:

### 13.1 FORTRAN-Style Comment Line

A punched card with **`C` in column 1** is treated as a comment line in its entirety. This is identical to FORTRAN comment lines.

### 13.2 Column 73–80 Comments

Columns 73–80 of every card are always treated as a comment. This is the standard card-image comment field.

### 13.3 Period-Delimited Inline Comment

A **period `.`** in the address field terminates the address and begins an inline comment. This allows comments to appear earlier than column 43 (the default start of the comment field for most statements):

```
    ,AAX, =:774 . EXTRACT EXPONENT FIELD
    ,E+N, 64    . NORMALIZE
```

The period syntax is particularly useful because it keeps the explanatory text immediately adjacent to the code it describes.

Long statements (ISO, BLOCK, CONT, and a few others) extend their effective column range to 72. For all other statements, the comment field automatically begins at column 43 even without a period.

To extend a statement past column 42, punch `L` in column 1 of the next card (continuation marker). In practice, this is rarely needed.

---

## 14. Subprogram Design Rules

Madlen subprograms can be written in non-standard ways, but adhering to the conventions described in this section makes a subprogram interoperable with FORTRAN and ALGOL code, loadable at any address, and depositable in the general-purpose library.

### 14.1 Index Register and Mode Register Conventions

The Dubna monitor system establishes the following conventions for all subprograms:

#### Index Register Usage

| Register(s) | Convention | Notes |
|-------------|-----------|-------|
| M[1]–M[7] | **Caller-saved**: must be restored before return | If you use these, save and restore their values. |
| M[8]–M[12], M[14] | **Callee-clobberable**: may be modified freely | The caller does not expect these to survive a call. |
| M[13] | **Return address register** | Contains the word address to which `13,UJ,` returns. If your subprogram calls other subprograms (thereby overwriting M[13]), you must save M[13] first. |
| M[14] | **Working register** | Also clobbered by *all* extracodes (no other index register is affected by extracodes). |
| M[15] | **Stack pointer** | Set by the monitor to 53401₈, 55401₈, or 73401₈ depending on the allocated memory. The stack region is always 377₈ = 255 words. Stack overflow or underflow is detected at runtime. |

#### Mode Register R Convention

The AU mode register R must have the value **6** both on entry to and on exit from any autocode subprogram. The value 6 in binary is `000 110₂`, which means:
- Bits 5–3 = `001` → logical ω mode
- Bit 2 = `1` → rounding suppressed
- Bit 1 = `0` → normalization enabled
- Bit 6 = `0` → overflow exceptions enabled

This corresponds to performing arithmetic with normalization and without rounding (the standard BESM-6 floating-point mode).

The standard instruction to establish this value is:
```
    ,NTR, 6
```

(NTR = set mode register immediate; EA[6:1] = 6.)

Any subprogram that changes R during computation must restore it to 6 before returning.

### 14.2 Parameter Passing Convention (FORTRAN/ALGOL Compatibility)

FORTRAN and ALGOL pass parameters **by reference**: the *address* of each actual parameter is passed, not its value. In Madlen, these addresses are passed through the stack (M[15]).

#### Calling a Subprogram with Parameters

To call `SUB(A, B, C)` (analogous to FORTRAN `CALL SUB(A, B, C)`):

```
    14 ,VTM, A       ; M[14] ← address of A
       ,ITS, 14      ; push A onto stack; M[14] → A
    14 ,VTM, B
       ,ITS, 14
    14 ,VTM, C
       ,ITS, 14
       ,CALL, SUB    ; call SUB; return address saved in M[13]
```

This sequence pushes the accumulator's current value first (the `ITS` instruction always saves A before loading), then the addresses of the actual parameters in order A, B, C. After the call, M[15] has been decremented by the number of parameters (3 in this case), plus one for the accumulator save.

#### Calling a FUNCTION

For a FUNCTION (which returns a value), the call sequence is the same. After `CALL` returns, the function value is in the accumulator. To save it and restore the pre-call accumulator:

```
    ,STX, RESULT      ; save function value; pop stack (restores old A)
```

Alternatively, if the first parameter address was pushed with `ITA` instead of `ITS`:
```
    14 ,VTM, A
       ,ITA, 14       ; load M[14] into A (non-stack mode — does NOT push old A)
    ...
       ,CALL, FUNC    ; result in A; nothing extra to pop
```

#### Extracting Parameters Inside the Callee

Parameters are extracted in **reverse order** (the last parameter is on top of the stack / in the accumulator):

```
    ,STI, 14    ; M[14] ← low 15 bits of A (= address of C); pop stack
    ,STI, 12    ; M[12] ← address of B; pop stack
    ,ATI, 11    ; M[11] ← low 15 bits of A (= address of A); A unchanged
```

After extracting N parameters, the stack must be restored to its pre-call state. For a SUBROUTINE with N parameters, pop N times total. For a FUNCTION with N parameters, pop N−1 times (the last pop happens implicitly when the caller receives the return value).

If there are too many parameters to store in index registers simultaneously, addresses can be spilled to memory cells.

#### A Parameterless SUBROUTINE

A SUBROUTINE with no parameters must still preserve and restore the accumulator, because the call protocol places an accumulator snapshot on the stack:

```
    ; Entry:
    15 ,ATX,          ; save current A to top of stack; push
    ; ... body ...
    ; Exit:
    15 ,XTA,          ; restore A from stack; pop
    13 ,UJ,           ; return
```

or equivalently using `XTS`/`STX`.

#### The PROGRAM Entry Point

The top-level FORTRAN PROGRAM unit corresponds to a subprogram with:
```
    PROGRAM: ,NAME,
```
or an entry point:
```
    PROGRAM: ,ENTRY,
```

### 14.3 Naming Conventions

#### Subprogram Names

For a Madlen subprogram (or entry point) to be callable from FORTRAN or ALGOL, its name must satisfy:
- At most **6 characters**.
- Contains only characters valid for a FORTRAN identifier (Latin letters and digits; no `*` or `/`).

Madlen allows longer names and `*`/`/` characters, but these are then inaccessible from FORTRAN/ALGOL.

#### COMMON Block Names

A Madlen COMMON block corresponding to a FORTRAN `COMMON /name/` block must have its block identifier **enclosed in asterisks**:

```
    *BLOCK1* : LC ,BLOCK, A(10), B(25)
```
matches the FORTRAN declaration:
```fortran
    COMMON /BLOCK1/ A(10), B(25)
```

A block identifier without enclosing asterisks (e.g., `BLOCK1`) is private to the autocode world and is inaccessible to FORTRAN subprograms.

### 14.4 Standard Subprogram Structure

The canonical layout of a Madlen subprogram is:

```
    Header (NAME)
    Executable commands
    Constants and BSS declarations
    DATA block
    SET (distribution) directives
    ,END,
```

Comments may appear before the header and anywhere up to END. Declarations (EQU, SUBP, ENTRY, BLOCK) may appear anywhere after the header, with the constraint that any identifier used in an address must already have been declared. BSS statements with non-empty addresses (i.e., those that actually reserve memory) may be interleaved with constants; they are conventionally placed after the constants. Empty BSS statements may appear anywhere. DATA and SET must come last.

---

## 15. Addressing and Basing

### 15.1 The Short-Address Problem

BESM-6 instruction encoding provides two formats:

- **Format 1** (short address): 12-bit offset in bits 12–1, giving an unsigned address range of 0–4095 (0000₈–7777₈). With the index register contribution, the effective address can be any value in 0–32767, but the *offset in the instruction* must fit in 12 bits.
- **Format 2** (long address): 15-bit offset in bits 15–1, reaching any address 0–32767 (00000₈–77777₈) directly.

Most data-handling and arithmetic instructions use Format 1. This means that when a subprogram is loaded at an address greater than 07777₈ (4095 decimal), Format 1 instructions cannot directly reference internal data addresses — the full address minus the subprogram's load base exceeds 12 bits.

The simplest workaround is to use two instructions:

```
    ,UTC, A      ; set C = A (C register is added to next instruction's EA)
    ,XTA,        ; load mem[0 + C] = mem[A]
```

or:

```
    J ,VTM, A    ; M[J] ← A
    J ,XTA,      ; load mem[0 + M[J]] = mem[A]
```

Both convert the reference to a long-address computation but require an extra instruction per access. For subprograms with many internal references, the *basing* mechanism is more efficient.

### 15.2 Local Basing — Single Base Register

*Local basing* (the most common form) converts short-address instructions automatically during translation, eliminating the extra instruction per access.

#### Concept

Every internal address can be written as `*C + D`, where `*C` is the (unknown) load address of the subprogram and `D` is the offset within the subprogram (which never exceeds the subprogram's length — typically less than 07777₈). An instruction of the form:

```
    ,XTA, A
```

is rewritten by the translator as:

```
    J ,XTA, A - *C
```

where `J` is the base register and `A - *C` is a small non-negative offset. At runtime, M[J] holds the actual load address `*C`, so the effective address is M[J] + (A − *C) = A as required.

#### Requesting Basing

To activate basing, write:

```
    I ,BASE, *C
```

where `I` is the chosen base register. This is equivalent to:

```
    I ,BAS,  *C   ; from this point on, base register I, base address *C
    I ,VTM,  *C   ; load *C into M[I] at runtime
```

The `BASE` form both declares the basing and emits the VTM instruction to load the base register. The `BAS` form only declares basing (useful when the base register is already set by other means).

#### Conditions for Basing

The translator will automatically base a command if and only if **all three** of the following conditions hold:

1. The command uses a **Format 1 (short address)** instruction.
2. The command has an **empty** index register selector.
3. The command's full address is an **internal address** of the subprogram (i.e., defined by a label, BSS, or EQU relative to another internal address).

Commands with an explicit non-zero index register and an internal address are *not* automatically based. Instead, they are expanded into a two-instruction sequence:

```
    M ,XTA, A
```
becomes:
```
    ,UTC, A
    M ,XTA,
```

#### Cancelling Basing

```
    I ,BAS,
```
(empty address) cancels basing. A new `I ,BASE, *C` statement also cancels the previous basing and establishes a new one.

#### Valid Base Address Range

The base address must be close enough to all based instructions so that the offset `A − *C` fits in 12 bits:

```
    *C - 10000₈ ≤ K ≤ *C + 7777₈
```

for every internal address `K` that is to be based. If this range is exceeded, the translator reports a fatal basing error.

#### Choosing the Base Register

- If the subprogram does not call other subprograms, registers M[8]–M[12] are appropriate (callee-clobberable; freed for use here).
- If the subprogram calls other subprograms, the base register must survive those calls, so M[1]–M[7] (caller-saved) should be used.

Precise information about which addresses were based and which were not is always available in the standard module listing (see [Section 17](#17-standard-module-output-format)). A based instruction shows the base register in its translated form.

### 15.3 Global Basing — Three Base Registers

*Global basing* covers the full 15-bit address space using three registers simultaneously. It is requested by specifying the base register number in the NAME header:

```
    MYSUB : R ,NAME,
```

where `R` is a decimal integer 1–13. The loader assigns three consecutive registers R, R+1, R+2 with values **20000₈**, **40000₈**, and **60000₈** respectively.

The coverage argument: the full address space is 0–77777₈. Addresses 0–7777₈ and 67777₈–77777₈ (within ±10000₈ of 0 or 0100000) do not need basing. The remaining addresses can each be reached within 10000₈ from one of 20000₈, 40000₈, or 60000₈. The loader chooses the appropriate register for each instruction.

Global basing applies to all Format 1 commands with:
- An empty index register selector, **and**
- An address field greater than 07777₈.

This automatically excludes instructions like `ASN`, `NTR`, `ATI`, etc., whose address fields have architectural meanings (they are N-type, not memory addresses) and must not be modified.

Commands with Format 1 and a non-empty index register are still expanded into two-instruction sequences (UTC + command), as in local basing. If you know that these do not need basing, use:

```
    MYSUB : R ,NAME, ***
```

The `***` suffix suppresses the automatic expansion of indexed short-address commands.

**Important**: the subprogram itself must load the base register values. Typically, near the entry point:

```
    R   ,VTM, 20000B
    R+1 ,VTM, 40000B
    R+2 ,VTM, 60000B
```

Parametric commands (Section 11) are never globally based.

### 15.4 Relocatable Basing — REL and RELS

For system subprograms that must be *position-independent* (executable without any pre-loading of base registers), the REL mechanism generates a self-contained relocatable subprogram.

**Request relocatable basing:**
```
    M ,REL, B
```
where `M` is the index register (typically M[14], set by the loader) and `B` is the start address of the range to be based.

**With external dependencies:**
```
    M ,RELS, B
```
Used when the based addresses include references to external objects.

**Cancel:**
```
    M ,REL,
```
(empty address).

---

## 16. Complete Examples

### 16.1 FUNCTION SCAL — Scalar Product

This example implements a function that computes the scalar (dot) product of two N-element floating-point vectors. It is structured as a FORTRAN-compatible FUNCTION with three parameters:

```fortran
    FUNCTION SCAL(A, B, N)
```

where A and B are arrays and N is the number of elements.

```
  SCAL:   ,NAME,       . FUNCTION SCAL (A, B, N)
          ,STI, 14     . extract N into M[14]; pop stack
          ,STI, 12     . extract address of B into M[12]; pop stack
          ,ATI, 11     . extract address of A into M[11]; A unchanged
          ,NTR, 3      . set R = 3 (normalization on, rounding off, mult. mode)
       14 ,XTA,        . A ← mem[M[14]] = N (load N by its address)
          ,UTC, =I1    . set C = 1 (*)
          ,X-A,        . A ← 1 - N  (= -(N-1), the initial loop counter)
          ,ATI, 14     . M[14] ← low 15 bits of A (= 1-N, negative count)
          ,NTR, 18     . set R = 18₈ = 010 000₂: mult. mode + suppress norm
          ,XTA,        . A ← 0 (load memory[0] = accumulator zero initializer)
    *1:   ,BSS,        . loop top (label *1 = address of next instruction)
       11 ,XTS,        . push A (partial sum); A ← mem[M[11]] = A[i]
       12 ,A*X,        . A ← A[i] * mem[M[12]] = A[i]*B[i]
       11 ,UTM, 1      . M[11] ← M[11] + 1 (advance pointer to A[i+1])
       12 ,UTM, 1      . M[12] ← M[12] + 1 (advance pointer to B[i+1])
       15 ,A+X,        . A ← A[i]*B[i] + mem[M[15]-1] (pop partial sum, add)
       14 ,VLM, *1     . M[14] ← M[14]+1; if M[14]≠0, branch to *1
          ,NTR, 6      . restore R = 6 (standard exit value)
       13 ,UJ,         . return (branch to M[13] = saved return address)
          ,END,
```

**Annotation:**

- **Parameter extraction**: `STI` pops the stack into an index register; `ATI` loads the index register from A without popping. Since the last parameter pushed is at the top of the stack (in A), we extract C first (STI→M[14]), then B (STI→M[12]), then A (ATI→M[11]).

- **`(*)`**: The instruction `,UTC, =I1` is the single short-address command that satisfies all basing conditions; the author notes that basing is not needed here since only one such command exists.

- **Loop counter**: Setting M[14] = 1−N (a negative value) allows the VLM instruction to count up toward zero. VLM increments M[14] and branches if the result is non-zero; when M[14] reaches 0, the loop exits.

- **NTR, 18**: Sets R = 18₈ = `010 000₂`. Bits 5–3 = `010` = multiplicative mode; bit 4 = `1` = suppress overflow. This is used for the accumulation loop because overflow suppression allows the running sum to grow without exception.

- **Return**: `13,UJ,` branches to M[13], which the CALL instruction set to the return address.

### 16.2 FUNCTION INTISO — Integer to ISO Encoding

This example converts a non-negative integer (less than 2¹⁸) to a 6-character ISO string, where each octal digit of the integer becomes its corresponding ISO character.

For example: the integer 21₁₀ = 000025₈ is converted to the ISO string `"000025"`.

The key insight: each octal digit maps to an ISO character by ORing with 60₈ (ISO `'0'` = 60₈ = 48₁₀ decimal). The AUX (unpack bits) instruction distributes the 18 data bits across all 6 bytes at once.

```
    INTISO:   ,NAME,       . FUNCTION INTISO (INT)
            8 ,BASE, *     . base register M[8] = start of subprogram
              ,ATI, 14     . M[14] ← low 15 bits of A (= address of INT)
           14 ,XTA,        . A ← mem[M[14]] = INT (load the integer)
              ,ASN, 64-30  . shift A left by 30 bits (put 18 bits at the top)
              ,AUX, =6H'7''7''7''7''7''7'  . unpack: spread 3+3+3+3+3+3 bits into 6 bytes
              ,AOX, =6H'60''60''60''60''60''60'  . OR each byte with 60₈ = ISO '0'
           13 ,UJ,          . return; result (6 ISO chars) in A
              ,END,
```

**Annotation:**

- **`8,BASE,*`**: Sets up local basing with M[8], using `*` (the current address) as the base. All subsequent short-address commands with internal addresses will be translated with M[8] as the index register.

- **`ATI, 14`**: M[14] ← address of INT (the single integer parameter, still in A from the caller's parameter push).

- **`14,XTA,`**: Loads the integer value from the address held in M[14].

- **`ASN, 64-30`**: The shift amount is `N - 64` where N = EA[7:1]. Here EA = 64-30 = 34; shift amount = 34-64 = -30, meaning a **left** shift by 30 bits. This moves the 18 low-order bits of the integer to bits 48–31 (the top 18 bits).

- **`AUX, mask`**: The mask `=6H'7''7''7''7''7''7'` is the ISO string `"\x07\x07\x07\x07\x07\x07"` — a 48-bit value with `000 111` in the top 3 bits of each 8-bit byte. AUX distributes the top bits of A according to the 1-bits in the mask: for each 1-bit in the mask (3 per byte × 6 bytes = 18 bits total), one bit from A's MSB is placed into that position. The result is that each octal digit (3 bits) ends up in bits 3–1 of its respective byte, with the upper 5 bits of each byte zeroed.

- **`AOX, =6H'60''60''60''60''60''60'`**: ORs each byte with 60₈, converting the octal digit value (0–7) in bits 3–1 to the corresponding ISO character code (60₈–67₈ = '0'–'7').

The result is a 6-byte ISO string in A, ready to be stored as the function's return value.

---

## 17. Standard Module Output Format

The *standard module* (standard array) is the binary load-module representation of the assembled subprogram. When a print mode is enabled (see [Section 19](#19-control-cards-editing-and-service)), the translator prints the standard module to the left of the autocode source listing.

### Listing Format

Each machine word is printed on **two lines**:
- The **upper line** corresponds to the **left instruction** (bits 48–25) and shows the relative address of the word in octal at the left margin.
- The **lower line** corresponds to the **right instruction** (bits 24–1).

Each instruction is printed in the machine-format notation:

```
    <index-reg-octal> <opcode-octal> <address-octal>
```

- For **Format 1** (short-address) instructions: index register in 2 octal digits, opcode in 2 octal digits, address in **4** octal digits.
- For **Format 2** (long-address) instructions: index register in 4 octal digits, opcode in 2 octal digits, address in **5** octal digits.

### Padding

If a machine word contains only a left instruction (the right instruction slot is empty), the translator fills it with:
```
    00 22 00000
```
This is a UTC with address 0 (a no-op: C ← 0, which has no effect).

### Based Instructions

When an instruction has been based, the listing shows:
- The base register number in the index register field.
- The address field contains the **difference** between the instruction's full address and the base address (i.e., the basing offset).
- If the offset is negative, the address is shown as `4000₈ + N`, where N is a row number in the description table.

### Literal Addresses in the Listing

Literal addresses are based if basing is active. A based literal address is shown with the base register and the offset from the base.

### Description Table References

Without basing, any non-absolute short address is shown as `4000₈ + N`, a reference to the N-th entry in the description table. The loader resolves these references at load time.

Long addresses appear as:
- `40000₈ + N`: the N-th address relative to the start of the subprogram.
- `74000₈ + N`: the N-th row of the description table.

The zeroth row of the description table always contains the subprogram's name in TEXT encoding.

---

## 18. Error Diagnostics

The Madlen translator performs syntactic and semantic validation during translation. It detects *formal* errors — those apparent from the source text — but cannot detect errors that manifest only at load time (such as using a short-address instruction without basing when the subprogram is loaded above address 07777₈).

### Error Reporting Style

For each erroneous statement, the translator may produce **multiple diagnostic messages**, printed immediately **before** the offending statement in the listing. Depending on the error type, the translator either ignores the entire statement or ignores only the erroneous portion.

### Fatal Errors (Translation Halts)

The following errors immediately terminate translation:

| # | Condition | Diagnostic message (Russian original) |
|---|-----------|----------------------------------------|
| 1 | No subprogram header (NAME) found | ОТСУТСТВУЕТ ЗАГОЛОВОК ПОДПРОГРАММЫ |
| 2 | Too many identifiers, or more than 20 entry points | ПЕРЕПОЛНЕНА ТАБЛИЦА ОПИСАНИЙ |
| 3 | Subprogram exceeds 23 pages (pages = 1024-word units) | ДЛИНА ПОДПРОГРАММЫ ПРЕВЫШАЕТ ВОЗМОЖНОСТИ МАШИНЫ |
| 4 | More than 100 erroneous statements encountered | ОЧЕНЬ МНОГО ОШИБОК |
| 5 | BAS/BASE address resolution error | НЕЯВНООПРЕДЕЛЕННЫЙ ИДЕНТИФИКАТОР BAS |

### Non-Fatal Errors (Translation Continues)

The most common non-fatal errors:
- **Undefined identifier**: an identifier appears in an address field without having been declared (via label, BSS, EQU, or BLOCK).
- **Multiply-defined identifier**: an identifier is given more than one definition (e.g., appears as a label twice, or is defined by both a label and an EQU).

The translator continues past these errors, which allows a single translation run to report all errors in the subprogram.

---

## 19. Control Cards, Editing, and Service

Control cards are special punched cards interspersed with the autocode source. They configure the translator's behavior and output format.

### Required Control Card

Every autocode source deck must be preceded by:

```
    *ASSEMBLER
```

This card signals to the Dubna monitor that the following cards contain Madlen autocode. Without it, the monitor does not invoke the assembler.

### Print Mode Control

By default (no special cards, and *NO LIST not specified), the translator prints the edited autocode source in **two-column format** (bilisting), without the standard module.

To obtain the standard module listing, add the following two consecutive cards before the autocode source:

```
    *CALL PUTFLAG*
    n
```

where `n` is:
- `0`: two-column (bilisting) format
- `4` or `6`: single-column format

In two-column bilisting, long statements are printed in two lines.

### Full Listing

```
    *FULL LIST
```

After the subprogram text, the following additional tables are printed:
- **Description table** (таблица описаний): all declared identifiers with their values.
- **Reference table** (таблица ссылок): all identifiers that were referenced.
- **Unused identifiers list**: identifiers declared but never used.

`*FULL LIST` and `*NO LIST` may also appear **inside** the subprogram text (before the first command) to enable or disable printing for a portion of the subprogram. The outer print mode is saved and restored after the subprogram is translated.

### Suppress Listing

```
    *NO LIST
```

Suppresses all output for the following subprogram.

### Absolute Address Mode

```
    *MOSU A
```

where `A` is a 5-digit octal address. Causes the listing to print addresses starting from `A` instead of from `00000`. This is used when a subprogram is known to be loaded at a specific absolute address (common in system programming).

### Editing

The translator always *edits* the autocode text in the listing: it normalizes the positions of delimiters (colons, commas, periods), left-aligns labels and address fields, and right-aligns index register selectors. Erroneous statements are printed without editing, showing the raw input.

---

## 20. Tips and Recommendations

### 20.1 Testing at High Load Addresses

When debugging a small autocode subprogram in isolation, the loader may place it at addresses below 07777₈. In that case, missing or incorrect basing may not cause failures, because the short addresses happen to work without modification. When the same subprogram is later included in a larger program and loaded above 07777₈, the basing error becomes apparent and the program fails.

**Recommendation**: Include a dummy array of at least 3600 decimal words in the test PROGRAM unit:

```fortran
    DIMENSION DUMMY(3600)
```

This forces the loader to place subsequent subprograms at addresses above 07777₈, exposing basing errors during isolated testing.

### 20.2 Loading Subprograms at Low Addresses

If a subprogram specifically needs to be loaded at an address below 07777₈ (rare, but occurs for system programs):
- Ensure the FORTRAN PROGRAM unit declares no large arrays.
- Declare the subprogram with EXTERNAL in the PROGRAM:
  ```fortran
      EXTERNAL SUB
  ```
  Subprograms declared EXTERNAL are loaded in reverse order of declaration, so the last EXTERNAL is loaded first (at the lowest address).

### 20.3 Multiple Entry Points

When writing a subprogram with multiple entries (ENTRY statements), each entry must be treated as a fully self-contained subprogram:

- Extract actual parameter addresses from the stack (STI/ATI sequence).
- Request and set up basing.
- Set up the R mode register.
- End with `13,UJ,` (or equivalent RETURN).

Each entry must set up its own basing, because M[8] (or whichever base register is used) is callee-clobberable and may have been changed by a previous call.

### 20.4 Debug Output Routines

The Dubna system provides several standard output subprograms for debugging:

#### PRINT8 — Text Output (Non-Standard Call)

```
    14 ,VTM, <start-address-of-text>
       ,ITS, 14
    14 ,VTM, <end-address-of-text>
       ,ITS, 14
    14 ,VTM, <starting-column-number>
       ,ITS, 14
       ,CALL, PRINT8
```

The "non-standard" aspect: the last parameter pushed is the column number itself (an integer), not the *address* of a variable holding the column number. Columns are numbered from 0; column 0 is the carriage-control position. Avoid using column 0. Only one text line is printed per call; excess characters are truncated.

#### PRINTA — Text Output (Standard Call)

```fortran
    CALL PRINTA(ASTART, AEND, NPOS)
```

Standard call passing addresses of ASTART, AEND, and NPOS by reference. Prints the ISO text from address ASTART to AEND, starting at printer column NPOS.

#### PRINTE — Floating-Point Array Output (E Format)

```fortran
    CALL PRINTE(ASTART, AEND, N, M)
```

Prints the floating-point array from ASTART to AEND, N numbers per line, M digits after the decimal point. Field width per number is M+7 characters, separated by single spaces.

#### PRINTO — Octal Output

```fortran
    CALL PRINTO(ASTART, AEND, N, M)
```

Prints numbers in octal. Requires M ≤ 16. If M < 16, the most-significant digits are suppressed.

---

## 21. Programming Techniques

Autocode is typically employed when bit-level manipulation of machine words is required — a common need when interfacing with the BESM-6's packed data formats or when squeezing maximum performance from tight inner loops.

### 21.1 Bit Packing (Left-to-Right)

To append `k` bits from the low-order end of word `BIT` to the right end of the accumulating word `PACK`, shifting PACK left by `k` positions first:

```
       ,XTA, PACK        ; A ← current packed word
       ,ASN, 64-k        ; shift A left by k bits (shift amount = 64-k, then -64 gives -k, i.e., left k)
       ,XTS, BIT         ; push A (shifted PACK); A ← BIT
       ,AAX, =k-bits-mask ; A ← BIT & mask (isolate the k low bits)
    15 ,AOX,             ; A ← mask(BIT) | mem[M[15]-1] (pop shifted PACK, OR in the new bits)
       ,ATX, PACK        ; store result back to PACK
```

**Concrete example** (appending 6 bits from BIT):
```
       ,XTA, PACK
       ,ASN, 64-6       ; left shift by 6
       ,XTS, BIT
       ,AAX, =77        ; mask = 77₈ = 6 low bits
    15 ,AOX,            ; OR with top of stack (shifted PACK)
       ,ATX, PACK
```

For right-to-left packing, place the fragment in the most-significant bits and right-shift each time.

### 21.2 Bit Unpacking (Left-to-Right)

To extract the `k` most-significant bits of word `DEP`, leaving the remainder back in `DEP` and placing the extracted bits as an integer in word `INT`:

```
    ,XTA, DEP            ; A ← DEP
    ,ASN, 64-k           ; left shift by k: extracted bits overflow into Y (RMR)
    ,ATX, DEP            ; DEP ← A (remainder, shifted up; the k bits are gone)
    ,YTA,                ; A ← Y (the k extracted bits, in the most-significant positions)
    ,AOX, =I0            ; A ← Y | 0 (no-op for value, but sets ω to logical mode and clears Y)
    ,ATX, INT            ; INT ← A (the extracted k bits as an integer)
```

**Concrete example** (extracting 10 high bits of DEP):
```
    ,XTA, DEP
    ,ASN, 64-10         ; left shift by 10; bits 48–39 of old DEP go into Y
    ,ATX, DEP           ; store remainder
    ,YTA,               ; A ← Y (bits 48–39 of old DEP, now in high positions of Y)
    ,AOX, =I0           ; A ← Y | 0
    ,ATX, INT           ; INT ← extracted bits
```

For right-to-left unpacking, right-shift instead of left-shift, and extract from the low-order end.

### 21.3 Integer Arithmetic

The BESM-6's arithmetic instructions operate on the floating-point representation of A. For computation with integers (non-normalized numbers with exponent 40), multiplication and division require system subprograms:

#### Integer Multiply — `I*MU*I`

```
       ,XTA, M          ; A ← first operand (integer at address M)
    14 ,VTM, N          ; M[14] ← address of second operand
       ,CALL, I*MU*I    ; result in A, mod 2^40
```

#### Integer Divide — `I*DI*I`

```
       ,XTA, M          ; A ← dividend
    14 ,VTM, N          ; M[14] ← address of divisor
       ,CALL, I*DI*I    ; quotient in A (integer)
```

After the call, the result is in A as a BESM-6 integer (exponent 40, mantissa = the integer value). The result is taken mod 2⁴⁰.

Similar system subprograms exist for complex and double-precision arithmetic.

### 21.4 Using the AU Mode Register for Arithmetic Control

Recall that R = 6 = `000 110₂` on entry/exit (bits: overflow-enable, logical ω, rounding-suppress, normalization-enable). Within a subprogram, you may temporarily change R for specific purposes:

- `NTR, 1` — set R = 1: normalization disabled (useful for packing raw bit patterns without normalization moving bits around).
- `NTR, 3` — set R = 3: multiplicative ω mode with rounding suppressed; useful for the multiply-accumulate inner loop in SCAL above.
- `NTR, 0` — set R = 0 (no ω mode, no suppression): branch instructions U1A/UZA always/never branch.

Always restore with `NTR, 6` before returning.

---

## Conclusion

This article has described all constructs of the Madlen autocode as of 1975. The language continues to evolve; new constructs may appear in later versions.

The programmer using autocode must keep in mind that the Madlen translator is purely a *translator* — a language converter. It does not generate machine instructions beyond those explicitly written; every operation the subprogram must perform must be expressed as an autocode command. The only exception is the distribution of data values, which the loader performs according to the SET directives.

The translator requires that the source text be:

1. **Syntactically correct**: all statements must conform to the formats described in this article.
2. **Complete**: every identifier used in an address must have exactly one definition (label, BSS, EQU, BLOCK, etc.).
3. **Consistent**: no identifier may be defined more than once.

Violations of conditions 2 and 3 are detected during translation and reported as errors, allowing the programmer to correct them before attempting execution. Violations of the short-address constraints (using Format 1 instructions without basing when loaded above 07777₈) are *not* detected during translation and can only be caught by testing at realistic load addresses.

---

*See also:*
- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — complete instruction set reference with encoding, register descriptions, and arithmetic details.
- [Technical_Reference.md](../../docs/Technical_Reference.md) — C compiler pipeline and TAC IR reference.
