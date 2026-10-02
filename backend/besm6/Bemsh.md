# Bemsh: The BESM-6 Autocode (Shtarkman, 1967)

*(Based on V. S. Shtarkman, «Автокод для БЭСМ‑6. Описание языка (инструкция)»,
Order of Lenin Institute of Applied Mathematics, USSR Academy of Sciences, May 1967.)*

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Statement Format](#2-statement-format)
3. [Character Set and Encoding](#3-character-set-and-encoding)
4. [Labels](#4-labels)
5. [The Address Counter](#5-the-address-counter)
6. [Self-Defining Values and Expressions](#6-self-defining-values-and-expressions)
7. [Machine Instructions](#7-machine-instructions)
8. [Translator Control Commands](#8-translator-control-commands)
9. [Translator Defining Commands](#9-translator-defining-commands)
10. [Basing Commands](#10-basing-commands)
11. [Program-Linking Commands](#11-program-linking-commands)
12. [The Blankless Statement Form](#12-the-blankless-statement-form)
13. [Quick Reference](#13-quick-reference)
14. [Assembling and Running Bemsh on Dubna (toolchain notes)](#14-assembling-and-running-bemsh-on-dubna-toolchain-notes)

---

## 1. Introduction

**Bemsh** is a symbolic-programming language — an *autocode* (автокод), in Soviet computing
terminology — for the BESM-6 computer. It was described by V. S. Shtarkman in 1967 at the
Institute of Applied Mathematics. Bemsh gives the programmer convenient means for writing machine
instructions, distributing memory and registers, and defining constants and data. From the point
of view of exploiting every feature of the machine, programming in Bemsh is equivalent to
programming directly in machine instructions: any machine program can be written in Bemsh, and the
language is therefore aimed at programmers who know the structure and capabilities of the BESM-6
well. In a sense, Bemsh is a tool for *hand* programming.

A program written in Bemsh is called the **source program** (исходная программа). It is converted
to the machine's internal language by a special program — the **translator** (транслятор). During
translation the translator performs several auxiliary functions, some automatically and some under
the control of special *translator commands* written by the programmer. Strictly speaking, the
translator produces a program that is not yet fully ready to run: final settling is done at
**load** time by another program, the **loader** (загрузчик), which may run arbitrarily long after
translation.

### Key properties

- **Mnemonic operation codes.** Every machine instruction has a mnemonic. For example, multiply
  (binary opcode `00 1111`) is written `УМН`.
- **Symbolic addressing.** Objects (instructions, constants, data arrays) may be given symbolic
  names (labels), and instructions address them by name rather than by numeric machine address.
- **Rich constant notation.** Numeric and logical constants may be written in decimal, binary,
  octal, or hexadecimal, as floating-point numbers, as text, or as machine-command words; the
  translator converts them to the binary machine form.
- **Label aliasing.** One label may be equated to another so that both address the same object,
  allowing different names in different parts of a program.
- **Relocatable output.** The translator's output is normally a *relocatable* program: the loader
  may settle it to run anywhere in memory. Producing a non-relocatable program takes *more* effort
  than producing a relocatable one.
- **Program linking.** Several independently translated programs can work together; the loader
  joins them and resolves the symbolic names of shared objects (linkage labels).
- **Diagnostic listing.** Each translation produces a printed document containing both the source
  program and its machine-form translation. Translation always runs to the end, so a single run
  reports *all* syntax errors.

### Relation to machine instructions

Bemsh is essentially a "one-for-one" language: translating one machine-instruction statement
produces exactly one machine instruction. There are only **two** documented exceptions:

1. **Literal commands** (see [§7.6](#76-literal-commands)) — a command whose operand describes a
   constant causes the translator to additionally allocate that constant in a separate word.
2. **Right half-word padding** (see [§7.7](#77-placement-of-instructions-in-memory)) — when a
   labeled item must start a fresh word, the translator fills the unused right half of the previous
   word with a do-nothing `МОДА` instruction.

For the bit-level encoding, register set, floating-point format, and operational semantics of every
machine instruction, see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md). For the BESM-6 data
representation see [Besm6_Data_Representation.md](Besm6_Data_Representation.md).

### How Bemsh differs from Madlen

Bemsh and [Madlen](Madlen.md) are both BESM-6 autocodes targeting the same machine and obeying the
same one-for-one principle, but they differ substantially in surface form:

| Aspect | Bemsh (1967) | Madlen |
|--------|--------------|--------|
| Mnemonics | Cyrillic (`УМН`, `СЛ`, `ПБ`) | Latin (`A*X`, `A+X`, `UJ`) |
| Directive names | Cyrillic (`СТАРТ`, `КОНД`, `ЭКВ`) | Latin (`NAME`, `INT`, `EQU`) |
| Statement fields | 5: name · operation · operand · comment · number | label · index · mnemonic · address |
| Primary delimiters | spaces (column form on the бланк) | two commas + colon |
| Max label length | 6 characters | 8 characters |
| Long vs short constant | `КОНД` (whole word) / `КОНК` (half word) | `INT`/`REAL`/`OCT`/`LOG` (whole word) |
| Octal self-defining value | `'375'` (single quotes) | `375B` (trailing `B`) |
| Index field in operand | parenthesized, e.g. `X(5)` | separate field before mnemonic |

The remainder of this manual presents each Cyrillic mnemonic and directive as primary, gives an
English gloss, and — for machine instructions — cross-references the equivalent Latin mnemonic from
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) and [Madlen.md](Madlen.md).

---

## 2. Statement Format

A Bemsh source program is a sequence of **statements** (предложения). Each statement specifies
either a machine instruction or a translator command. Statements are written on a coding form
(бланк) and then punched onto cards, **one statement per card**; cards with no punches are ignored.
The columns of the form correspond exactly to the character positions on the card.

### The five fields

A statement consists of one to five **fields**, laid out left to right:

| Field | Russian | Machine correspondence | Required? |
|-------|---------|------------------------|-----------|
| Name | название | address of the cell holding the instruction | optional |
| Operation | операция | the operation code | **mandatory** |
| Operand | операнд | the address part (including index register) | optional |
| Comment | комментарий | the note field | optional |
| Number | номер | card identification / sequence number | optional |

The general rules:

1. **The operation field is the only mandatory field.** The presence of the others depends on the
   operation and on the programmer's wishes.
2. **The name field must begin in the leftmost column** of the form. The other fields need only be
   ordered left to right and separated from one another by at least one space.
3. The name, operation, and operand fields **must not contain internal spaces** — except that a
   space may appear as a character inside a text constant in the operand field.
4. A statement begun on one line **cannot be continued** on the next.
5. **An asterisk in the leftmost column** marks the entire line as a comment.

Thus, spaces are the field separators in the standard (column) form. An alternative *blankless*
form using `:`, `,`, and `;` as separators is described in [§12](#12-the-blankless-statement-form).

### Field details

- **Name** — assigns a symbolic label to the statement (see [§4](#4-labels)). If present, it must
  start in the leftmost column and be at most 6 characters. A space in the leftmost column means
  "no label".
- **Operation** — one of the permitted machine mnemonics or translator-command mnemonics. A valid
  mnemonic is never more than five characters. If the operation is invalid, the translator treats
  the whole statement as a comment and prints an error.
- **Operand** — supplies the information the operation needs. For a machine instruction it
  determines the address part, including the index register. The operand field may consist of one
  or several **operands** separated by commas. Because a space terminates the *entire* operand
  field, no space may appear between operands and their separating commas. When a command has no
  operand but does have a comment, the empty operand may be indicated by a comma surrounded on both
  sides by at least one space.
- **Comment** — for the programmer's convenience only; carried into the printed document but with
  no effect on translation. May contain any permitted character, including spaces. A whole line may
  be a comment by placing `*` in the leftmost column; to continue a comment-only block, each
  continuation line must also begin with `*`.
- **Number** — for program identification and card numbering. To the translator it is
  indistinguishable from a comment.

### Example

```
ПРОБА   ПЕ    69            ; label ПРОБА, branch-if-omega-set to address 69
```

Here `ПРОБА` is the name, `ПЕ` the operation (branch if the result flag ω = 1), and `69` the
operand (branch target, decimal).

---

## 3. Character Set and Encoding

A Bemsh statement may use the following graphic characters:

1. **Capital Russian letters** А–Я (excluding the hard sign Ъ).
2. **Capital Latin letters** that do not coincide in shape with Russian letters:
   `D F G I J L N Q R S U V W Y Z`.
3. **Digits** 0–9.
4. **Operators and delimiters**: `* + - / , ( ) ' .` and the space.

These are a subset of the characters available on the line printer (АЦПУ-128) and the card-punch
preparation device (УПП). Additional characters available on those devices may be used in comments
and text constants.

Bemsh is oriented toward the Russian alphabet: the language's basic symbols (such as mnemonics) use
only Russian letters. Latin letters may be used in labels — the symbolic names the programmer
chooses. (Punching is actually easier with Russian-letter labels, since all Russian letters and
digits share one keyboard register on the УПП.) The translator is intended to support a purely
Latin "dialect" as well, so that foreign equipment can prepare cards; for this reason the operators
and delimiters were chosen from the set common to virtually all foreign keyboards.

**Encoding.** On input, the translator recodes all source information into a single internal code.
Different parts of a program may be prepared in different external encodings; each part with a new
encoding must be preceded by a `КОД` command (see [§8](#8-translator-control-commands)). The
internal code is an 8-bit extension of the 7-bit standard data-transmission code (СКПД), itself
based on ISO recommendations. (See also [KOI7_Encoding.md](KOI7_Encoding.md) for the related KOI-7
code page used elsewhere in this project.)

---

## 4. Labels

A **label** (метка) is a name the programmer creates to designate areas of memory, instructions,
I/O devices, or registers. (The term "label" here is broader than in ALGOL; it is a synonym for
ALGOL's *identifier*.)

- A label has **1 to 6 characters**, consisting of letters and digits, and **must begin with a
  letter**. Violating these rules causes the label to be rejected with an error message.

### Defining a label

Every label used in a program must be **defined**. A label is defined at the moment it is used as
the *name* of a statement; it then receives a value, which is substituted wherever the label
appears as an operand. A label that is never defined cannot be used as an operand. **Each label may
be defined only once**; a second appearance as a name is ignored and reported as an error.

- **Relocatable (relative) labels.** Usually a label's value is the address of the object described
  by the statement it names: for a machine instruction, the cell holding the instruction; for a
  memory array, the address of the array's first cell. Such values change when the program is
  relocated, and the label is called *relocatable*.
- **Absolute labels.** A label whose value is fixed and does not change with relocation — for
  example one naming an index register, an I/O device, or an absolute memory address. Absolute
  labels are defined with the `ЭКВ` command (see [§9.1](#91-экв--equivalence)).

At translation time there is always a *assumed starting address* for the program, either set by the
programmer or taken as zero. Cells are counted from this address, and that count fixes the values of
relocatable labels.

### Previously-defined labels

In some operand positions only labels that are **previously defined** are permitted — i.e. labels
that appeared as the name of an *earlier* statement. For example, the operands of `ЭКВ`, `ПАМ`, and
`КОНД` must be previously defined.

### Linkage labels

A label whose value is supplied by, or passed to, *another* independently translated program is a
**linkage label** (метка связи). These are introduced and resolved by the program-linking commands
`ВХОДН` and `ВНЕШН` and obtain concrete values only at load time. See
[§11](#11-program-linking-commands).

---

## 5. The Address Counter

The translator maintains an **address counter** (счётчик адреса) that advances as cells are
allocated to elements of the program. It always points at the next cell to be allocated.

- The counter advances by **one** after a cell is filled with a pair of machine instructions or a
  constant. A translator command describing an array advances it by the array's size. Some
  translator commands do not advance it at all.
- Because a BESM-6 instruction occupies **half** a cell, the counter has one fractional binary
  digit. Translating one machine instruction normally advances the counter by **one half**.
- Writing an asterisk `*` as an operand yields the current value of the counter (its integer part
  only). In a machine instruction, `*` is therefore the address of the cell holding that
  instruction; as an operand, `*` is a relocatable label.

The initial value of the counter is the assumed starting address of the program, set by the `СТАРТ`
command. The programmer may change it at any point with the `АДРЕС` command. The maximum counter
value (integer part) is **32767**; the next value is 0. Counter overflow is reported as an error but
does not stop translation.

Two instruction-placement consequences of the half-cell counter are described in
[§7.7](#77-placement-of-instructions-in-memory).

---

## 6. Self-Defining Values and Expressions

### Self-defining values

Besides labels and `*`, an operand may be a **self-defining value** (самоопределённая величина) — a
standard way to write an arbitrary numeric value, which may itself be named with `ЭКВ`. There are
two kinds:

- **Decimal** — an unsigned decimal integer not exceeding **32767**.
  Examples: `7`, `15`, `007`, `4095`, `10351`.
- **Octal** — an unsigned octal integer in single quotes, not exceeding `'77777'`.
  Examples: `'7'`, `'375'`, `'002'`, `'37763'`.

### Simple and composite expressions

A label or a single self-defining value is a **simple expression**. An arithmetic combination of
simple expressions is a **composite expression** (составное выражение). A composite expression may
appear in the operand field of machine instructions and translator commands.

Permitted operators: `+` (add), `-` (subtract), `*` (multiply), `/` (integer divide). Rules:

- A composite expression contains **at most three** simple components — there are no parentheses
  for grouping.
- Two simple expressions may not stand adjacent, nor two operators.
- If the expression begins with an operator, that operator may only be `-` (so `-` is both binary
  and unary; a single label or value preceded by `-` is a composite expression).
- `*` denotes both the address counter and multiplication; no ambiguity arises, because the address
  counter (being a relocatable label) can never be an operand of multiplication.

Valid simple: `МАССИВ` · `3` · `'25'` · `*` · `X` · `ALPHА`

Valid composite:
```
МАССИВ+10        КОНТАБ-НАЧТАБ      Х+14*100       ДЛИНА/25
*+КОНТАБ-НАЧТАБ  *-200              -25            АЛЬФА*3+К
```

Invalid:
```
МАССИВ+-10   (two operators in a row)
МАССИВ'25'   (two simple expressions adjacent)
+А           (expression begins with plus)
3**          (address counter as a factor of multiplication)
ДЛН/5+Р+В    (more than three components)
```

### Evaluation

The translator evaluates each operand expression up to its terminator (a comma, parenthesis, or
space, depending on context):

1. Each simple component is given its numeric value.
2. Arithmetic is performed **left to right, with `*` and `/` before `+` and `-`**. Thus `А+В*С` is
   `А+(В*С)`, not `(А+В)*С`.
3. The result is the value of the expression.

Division is **integer (truncating)** and therefore not commutative with multiplication: `3/7*10`
yields 0, whereas `3*10/7` yields 4. Dividing a non-zero value by zero is illegal; `0/0` yields 0.

### Absolute and relocatable expressions

An expression is **absolute** if (1) it consists only of absolute components, or (2) it contains
exactly two relocatable components, one with `+` and one with `-` (their difference is constant
under relocation).

An expression is **relocatable** if its value changes by *K* when the program is shifted by *K*
cells, and this holds by virtue of the expression's *structure*, not the particular component
values. Hence:

1. A relocatable expression must contain **one** relocatable label (not preceded by `-`) **or
   three** relocatable labels, exactly one of which is preceded by `-`.
2. A relocatable label may not be a factor in multiplication or division.

With П a relocatable label and А an absolute one:

```
Absolute:      П-П+5    А+14*25    4095    А*А    А/А
Relocatable:   П+2      П-8*А      П-П+П   *-177  П-А
Illegal:       П+П      П+П-А      (two relocatable labels)
               П*А                  (relocatable label multiplied)
               П+П+П                (no minus sign)
               А-П                  (single relocatable label with minus)
               П-П-П                (two minuses)
```

### Range restriction

The value of any expression must lie between **−32768 and +32767**; violation is reported. Some
contexts impose further restrictions, noted where they apply.

---

## 7. Machine Instructions

To write a machine instruction is to specify the values of all its fields. The mnemonic determines
the instruction's **structure** (the value of bit 20) and, accordingly, the function/control
opcode; the remaining fields come from the operand field.

### 7.1 Instruction structures

A BESM-6 instruction occupies 24 bits and comes in two structures, selected by bit 20:

- **First structure (bit 20 = 0)** — informally the **short** command. Fields:
  - **И** — index-register number.
  - **Ф** — the 6-bit operation code ("function", функция).
  - **С** — the 12-bit *short* offset (смещение).
  - **З** — the *sign of the offset* С: when 0, С is widened to 15 bits with three leading zeros;
    when 1, with three leading ones (so С spans −4096…+4095).
- **Second structure (bit 20 = 1)** — informally the **long** command. Fields:
  - **И** — index-register number.
  - **У** — the operation code ("control", управление; the long structure mainly holds control
    instructions).
  - **П** — the 15-bit *full* offset.

The two structures have the same length; "short" and "long" refer to the offset width. For the bit
layout (Format 1 / Format 2) see [Besm6_Instruction_Set.md §3](Besm6_Instruction_Set.md#3-instruction-formats).

### 7.2 Effective-address formation

The **effective address** (исполнительный адрес) is usually formed from three components:

1. The **offset** — the 15-bit П for long commands, or the sign-extended С for short commands.
2. The **index** — the 15-bit index register named by field И.
3. The special **modifier register М** — 15 bits.

The address is the sum of the three, modulo 2¹⁵ (carry out of the top bit is lost). After address
formation, register М is cleared in all commands except `МОД` and `МОДА`:

- **`МОДА`** sends its own formed address into М.
- **`МОД`** sends the low 15 bits of the operand fetched from memory into М.

This lets `МОД`/`МОДА` modify the address of the single following command. (In
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) the modifier register М is called the **C
register**, and `МОДА`/`МОД` are `UTC`/`WTC`.)

**Seven long commands** repurpose the index field И for other ends, so the index register does
**not** participate in their address formation:

| Bemsh | Meaning | Latin |
|-------|---------|-------|
| `УИА` | set index by address | `VTM` |
| `СЛИА` | add address to index | `UTM` |
| `ПВ` | jump with return (call) | `VJM` |
| `ПИО` | branch if index = 0 | `VZM` |
| `ПИНО` | branch if index ≠ 0 | `V1M` |
| `ЦИКЛ` | end of loop | `VLM` |
| `УИИ` | set index by index | `MTJ` |

### 7.3 The operand field of a machine instruction

The operand field may be empty or contain at most two expressions:

- The **address expression** (адресное выражение) supplies the offset (П or С).
- The **index expression** (индексное выражение), if present, supplies field И. It must be:
  1. enclosed in **parentheses**;
  2. **simple and absolute**;
  3. positive and not greater than **15**.

If both are present, the address expression comes first. Examples of index expressions:
`(0)`, `(15)`, `('17')`, `(К)`, `(ИНДЕКС)`. Examples of address expressions:
`X`, `ИКС+5`, `*-200/17`, `'1777'`.

If an expression is absent, the corresponding field is filled with zeros.

### 7.4 Long commands

For a long command the address expression may be any valid expression; its value goes into П. A
**negative** address value is encoded in П in **two's complement**:

```
-1  ≡  32767  ≡  '77777'
-2  ≡  32766  ≡  '77776'   …and so on.
```

### 7.5 Short commands

The operand syntax is the same, but the address and index expressions are not always independent:

- **Absolute address expression** — behaves like a long command: the value (with sign) goes into С,
  and field И receives the index expression (zeros if absent). The absolute value must lie in
  **−4096…+4095**.
- **Relocatable address expression with an index expression present** — the address is assumed to
  refer to the *privileged* memory region, so its value must lie in **−4096…+4095**. This imposes
  relocatability restrictions on the whole program (see below).
- **Relocatable address expression with no index expression** — the translator is free to pick a
  suitable index register as a *base* (see [§10](#10-basing-commands)), compute the offset, and
  fill И with the chosen register. If no suitable register is available, the address is again
  assumed to lie in the privileged region, and the relocatability restriction recurs. When several
  registers qualify, the one giving a positive and smallest-magnitude offset is preferred.

The **privileged region** is the address range −4096…+4095 (memory is viewed as a ring, with cell
32767 adjacent to cell 0). While execution stays within it, short commands are as capable as long
ones.

**Relocatability restrictions.** They are caused by short commands that have a relocatable address
expression *and* either an explicit index expression or no available base register. In such commands
the address value must stay within −4096…+4095, which constrains the assumed starting address. A
program may be "incorrect" for some, all, or no starting addresses; the translator reports the
admissible range and flags every command that imposes a restriction.

**Operand-field errors** (recapitulated): the index expression must be simple, absolute, and in
0…15; a long command's address value must be in −32768…+32767; a short command's address value must
be in −4096…+4095 whenever (1) it is absolute, (2) an index expression is given, or (3) no base is
available.

### 7.6 Literal commands

In a **literal command** (команда типа «литерал») the operand describes a *constant* instead of an
address. The translator allocates the constant in a separate word and forms its address in the
command, so a value can be written "literally" at the point of use. The constant description is
preceded by an **`=`** sign and uses exactly the same forms as the `КОНД` command
([§9.3](#93-конд--long-constant)), except that **no repeat coefficient** may be given.

```
СЛ   =Е'1.'     ; allocate the float 1.0 and add it to the accumulator
СЛЦ  =Ю'1'      ; allocate a low-order-bit unit and cyclically add it
```

All literal constants are gathered into a single array at the end of the program; **repeated
literals are allocated once**. Referencing a literal from a short command may need basing or incur a
relocatability restriction. Literal commands are one of the two places where the one-for-one
principle is broken.

### 7.7 Placement of instructions in memory

Because two instructions share a 48-bit cell and the machine can only address whole cells (so
control cannot be transferred to the *right* instruction of a cell), the translator:

1. Counts cells to **half-cell** precision (the fractional bit of the counter).
2. Places any **labeled** (named) command in the **left half** of a fresh cell, skipping the right
   half of the previous cell if necessary.
3. Skips right halves likewise when a whole-cell item (number or constant) follows an odd number of
   instructions.
4. Fills every skipped right half with a do-nothing `МОДА` instruction (zeros in fields И and П).

Right-half padding is the second place where the one-for-one principle is broken.

### 7.8 Complete machine-instruction table

Mnemonics are Bemsh (Cyrillic); the **Latin** column is the equivalent from
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md), where full operational semantics, the ω
condition system, and stack-mode behaviour are documented. Opcodes are octal. Commands marked
**(priv.)** are privileged.

#### First structure — short commands (opcodes 000–047)

| Bemsh | Opcode | Meaning | Latin |
|-------|--------|---------|-------|
| `ЗП`   | 000 | store accumulator | `ATX` |
| `ЗПМ`  | 001 | store and pop (stack mode) | `STX` |
| `РЕГ`  | 002 | modify special registers **(priv.)** | `MOD` |
| `СЧМ`  | 003 | push accumulator and load | `XTS` |
| `СЛ`   | 004 | floating-point add | `A+X` |
| `ВЧ`   | 005 | floating-point subtract | `A-X` |
| `ВЧОБ` | 006 | reverse subtract (X − A) | `X-A` |
| `ВЧАБ` | 007 | subtract absolute values | `AMX` |
| `СЧ`   | 010 | load accumulator | `XTA` |
| `И`    | 011 | bitwise AND | `AAX` |
| `НТЖ`  | 012 | bitwise XOR (non-equivalence, mod-2 add) | `AEX` |
| `СЛЦ`  | 013 | cyclic add (end-around carry) | `ARX` |
| `ЗНАК` | 014 | apply sign of operand (conditional negate) | `AVX` |
| `ИЛИ`  | 015 | bitwise OR | `AOX` |
| `ДЕЛ`  | 016 | floating-point divide | `A/X` |
| `УМН`  | 017 | floating-point multiply | `A*X` |
| `СБР`  | 020 | pack bits ("assembly") | `APX` |
| `РЗБ`  | 021 | unpack bits ("disassembly") | `AUX` |
| `ЧЕД`  | 022 | population count (number of ones) | `ACX` |
| `НЕД`  | 023 | position of highest set bit | `ANX` |
| `СЛП`  | 024 | add exponent from memory | `E+X` |
| `ВЧП`  | 025 | subtract exponent from memory | `E-X` |
| `СД`   | 026 | shift by exponent in memory | `ASX` |
| `РЖ`   | 027 | set mode register from memory | `XTR` |
| `СЧРЖ` | 030 | read mode register | `RTE` |
| `СЧМР` | 031 | read younger-bits register | `YTA` |
| —      | 032 | no mnemonic **(priv.)** | — |
| `УВВ`  | 033 | I/O control — the peripherals **(priv.)** | `EXT` |
| `СЛПА` | 034 | add exponent immediate | `E+N` |
| `ВЧПА` | 035 | subtract exponent immediate | `E-N` |
| `СДА`  | 036 | shift immediate | `ASN` |
| `РЖА`  | 037 | set mode register immediate | `NTR` |
| `УИ`   | 040 | accumulator → index register | `ATI` |
| `УИМ`  | 041 | store to index register and pop | `STI` |
| `СЧИ`  | 042 | index register → accumulator | `ITA` |
| `СЧИМ` | 043 | push accumulator, load index register | `ITS` |
| `УИИ`  | 044 | copy index register | `MTJ` |
| `СЛИ`  | 045 | add index registers | `J+M` |

Opcodes 046–047 are illegal. Opcodes **050–077** are **extracodes** (экстракоды) — the system-call
and math-library interface (square root, trig, logarithm, exponential, I/O, formatted print, system
operations). Bemsh names them by their opcode: `э74` is extracode 074. See
[Besm6_Instruction_Set.md §6](Besm6_Instruction_Set.md#6-instruction-reference), and
[Besm6_Intrinsics.md](Besm6_Intrinsics.md) for reaching them (and `УВВ`/`РЕГ`/`СТОП`) from C.

#### Second structure — long commands (opcodes 020–037)

| Bemsh | Opcode | Meaning | Latin |
|-------|--------|---------|-------|
| `Э20`  | 020 | extracode | — |
| `Э21`  | 021 | extracode | — |
| `МОДА` | 022 | send address to modifier register М | `UTC` |
| `МОД`  | 023 | send memory operand to modifier register М | `WTC` |
| `УИА`  | 024 | set index register by address | `VTM` |
| `СЛИА` | 025 | add address to index register | `UTM` |
| `ПО`   | 026 | branch if result flag ω = 0 | `UZA` |
| `ПЕ`   | 027 | branch if result flag ω = 1 | `U1A` |
| `ПБ`   | 030 | unconditional branch | `UJ` |
| `ПВ`   | 031 | jump with return (call) | `VJM` |
| `ВЫПР` | 032 | return from interrupt **(priv.)** | `IJ` |
| `СТОП` | 033 | stop the machine | `STOP` |
| `ПИО`  | 034 | branch if index register = 0 | `VZM` |
| `ПИНО` | 035 | branch if index register ≠ 0 | `V1M` |
| —      | 036 | free operation code | `*36` |
| `ЦИКЛ` | 037 | end of loop | `VLM` |

> **Note on `МОД`/`МОДА` opcodes.** The semantics in the 1967 source are unambiguous — `МОДА` loads
> register М from the *formed address*, `МОД` from the *memory operand* — and match the modern
> `UTC` (022) and `WTC` (023) of [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md). The opcode
> numbers above follow that authoritative reference; the PDF-extracted appendix of the original
> manual lists the two in the opposite numeric order, which is treated here as a transcription
> artifact.

---

## 8. Translator Control Commands

Translator commands (команды транслятора) direct the translator rather than the machine. **Control
commands** specify the encoding and form of the source, mark the start and end of translation, set
the address counter, and control the printed listing. **None of them generates a machine
instruction.**

Statement-format tables below use the three relevant fields: **Name** · **Operation** · **Operand**.

### `КОД` — source encoding

```
ignored          КОД      / encoding & dialect of the source /
```

Tells the translator on which device and in which dialect the source was prepared. If absent, the
УПП device and УПП code are assumed. Different parts of one program may use different encodings; each
new-encoding part must be preceded by its own `КОД`. The name field is ignored.

### `СТАРТ` — start of program

```
label (prog. name)   СТАРТ    self-defining value (assumed start address)
```

Marks the start of translation, names the program, and sets the assumed starting address. The name
field becomes the **program name** and is automatically specified as an entry point (no `ВХОДН`
needed); it can be referenced from other programs as an external label via `ВНЕШН`. If the name is
omitted, the program is named with six spaces. If the operand is empty, the starting address is 0.

`СТАРТ` is honored only when it is the **first** statement, or the **second** preceded by a `КОД`;
otherwise it is ignored. If absent altogether, the name is blank and the start address is 0.

```
ПРОГ2   СТАРТ   2040       ; address counter ← 2040, program named ПРОГ2
ПРОГ2   СТАРТ   '3770'     ; same (3770 octal = 2040 decimal)
```

### `АДРЕС` — new address-counter value

```
ignored          АДРЕС    relocatable expression (new counter value)
```

Sets the address counter to the value of a relocatable expression (the name field is ignored). May
be used anywhere, any number of times. All labels in the expression must be **previously defined**,
else the command is skipped with an error. Setting the counter below the `СТАРТ` starting value is
an error. An empty operand makes the command a no-op. `АДРЕС` also **aligns the translator to the
start of a cell** (padding a pending right half with a zero `МОДА`).

```
АДРЕС   *+500              ; advance the counter by 500 (cells not cleared)
```

`АДРЕС` is one way to reserve memory; the more usual way is `ПАМ`. Because `АДРЕС`'s operand must be
previously defined, an array boundary used in it must already be set, e.g. via a prior `ЭКВ`.

### `ФИНИШ` — end of program

```
ignored          ФИНИШ    relocatable expression (program entry point)
```

Stops translation. The operand names the point to which control is transferred when the loaded
program runs — usually the first machine instruction.

```
        СТАРТ   2000
МАССИВ  ПАМ     (100)
ВХОД    УИА     МАССИВ (13)
        ...
        ФИНИШ   ВХОД
```

### `СТРН` — new page

```
ignored          СТРН     ignored
```

Forces the next listing line onto a new page (the `СТРН` line itself prints before the page break).
Both name and operand are ignored. Useful for separating subprograms in the listing.

### `СТРОК` — skip lines

```
ignored          СТРОК    decimal number (lines to skip)
```

Inserts the given number of blank lines in the listing (not past the end of a page). An empty
operand skips one line.

---

## 9. Translator Defining Commands

**Defining commands** define labels, reserve memory areas, and store constants. The memory they
produce can be addressed by symbolic name. `ЭКВ` is grouped here because it defines a label.

### 9.1 `ЭКВ` — equivalence

```
label (defined label)    ЭКВ    expression (defining expression)
```

Defines the label in the name field, giving it the value of the operand expression. The expression
may be relocatable or absolute, and the label is defined accordingly. **All labels in the expression
must be previously defined.** If the label or expression is missing or malformed, the command is
ignored with an error.

`ЭКВ` equates a label with an index-register number, an I/O device number, an absolute address, or
any value; it can also abbreviate a frequently used composite expression, or alias one label to
another so the same object has different names in different parts of a program.

```
ВОЗВР    ЭКВ   13
МАГ      ЭКВ   15
ТАБЛА    ЭКВ   2000
РАЗМЕР   ЭКВ   150
ПОЛЕ     ЭКВ   АЛЬФА-БЕТА+ГАММА     ; АЛЬФА, БЕТА, ГАММА must be predefined
```

Labels denoting index-register numbers or non-memory ("address") values can be defined *only* by
`ЭКВ`; they cannot label memory locations.

### 9.2 `ПАМ` — reserve memory

```
label (array name)       ПАМ    self-defining value or predefined label (number of cells)
```

Reserves a memory area and names it. Processing:

1. If the counter is on a right half-cell, fill that half with a zero `МОДА` and advance to the next
   cell.
2. Take the counter value as the value of the name-field label.
3. Add the operand value to the counter.

`ПАМ` reserves the area "in place" — where the command appears. It does **not** clear the area, so
the program must not assume zeros there. To align the counter to the next whole cell, write
`ПАМ (0)` (reserve zero cells).

```
ТАБЛ1   ПАМ   50            ; 50-word area named ТАБЛ1
МАССИВ  ПАМ   (100)         ; 100-word array named МАССИВ
```

### 9.3 `КОНД` — long constant

```
label (constant name)    КОНД   one operand describing the constant (forms below)
```

Defines a **whole-cell** (48-bit) constant. The operand has one of two basic forms:

```
(p)T'c'        and        T(c)
```

- **(p)** — optional **repeat coefficient**: a self-defining value or a predefined absolute label,
  in parentheses; if omitted, one constant is produced. (Type **А** does not allow a repeat.)
- **T** — the **type letter** (table below).
- **'c'** — the constant body in single quotes (for type **А** the defining expression is in
  parentheses instead).

| Letter | Type |
|--------|------|
| `Ф`, `F` | integer, decimal |
| `Ю`, `U` | integer, binary |
| `В`       | integer, octal |
| `Х`       | integer, hexadecimal |
| `Е`       | floating-point number |
| `А`       | address (expression) |
| `К`       | machine-command word |

(For types having both a Russian and a Latin letter, either may be used.)

#### Integer constants (Ф/F, Ю/U, В, Х)

These differ only in the radix of the signed source integer. The result is the binary value placed
in all 48 bits of the word (treated as an integer); negatives are in two's complement; for positives
the `+` may be dropped. Loss of significant bits on the left is reported. In hexadecimal (`Х`),
digits above 9 are the first Latin letters A–F (with Russian equivalents for those that differ in
shape: `Д` = D, `Ф` = F).

The following all set only bit 8 of the word:

```
КОНД   Ю'10000000'
КОНД   В'200'
КОНД   Ф'128'
КОНД   Х'80'
```

To set every bit *except* bit 8: `КОНД В'-201'`.

#### Floating-point constant (Е)

Written as a signed decimal **mantissa** with an optional decimal point, followed by an optional
**order** — the letter `Е` and a signed decimal power of ten. The mantissa magnitude (ignoring the
point) must not exceed 1 099 511 627 775. The result is a 48-bit **normalized** floating-point word.
An out-of-range mantissa or number is reported. The following are all 3.1415:

```
КОНД   Е'3.1415'
КОНД   Е'31415Е-4'
КОНД   Е'+31415.E-4'
КОНД   Е'.31415E1'
КОНД   Е'.031415E+2'
```

#### Command constant (К)

The constant is a 24-bit machine command written in the conventional BESM-6 digit grouping: one
binary digit, one octal digit, one quaternary digit, then six octal digits. The command is placed in
the **right** half of the cell; the left half is zeroed.

```
КОНД   К'173777777'        ; a command word of all ones
```

#### Address constant (А)

A relocatable or absolute **expression** in parentheses (not quotes). Its value is stored as an
integer (units in the rightmost bit); negatives in two's complement. When the expression is
relocatable, the stored value is adjusted by the loader. No repeat coefficient is allowed.

```
ПРОГ1   СТАРТ
        КОНД   А(*)        ; first cell holds the program's own start address
```

### 9.4 `КОНК` — short constant

```
label (constant name)    КОНК   one operand describing the constant
```

Like `КОНД`, but: (1) the constant occupies the **next half-cell**, not a whole cell; (2) **no
repeat coefficient**; (3) **type Е is not allowed**. Otherwise the constant forms are identical. The
final 24-bit value is obtained by dropping the left half of the word that `КОНД` would have formed;
loss of significant bits in that left half is reported.

### 9.5 `ТЕКСТ` — text constant

```
label (constant name)    ТЕКСТ   text constant (form below)
```

Defines a sequence of alphanumeric characters. The operand form is:

```
T'c'
```

where `T` is the type letter `Т` and `'c'` is the text in single quotes. On input, characters are
recoded into the internal code (8 bits per character). Characters are packed **6 per cell**, left to
right; a text constant always occupies a whole number of cells, the last padded with spaces. The
length is limited only by the line width. **A space is a valid character.** A literal single quote
is written as **two** single quotes.

```
ТЕКСТ   Т'ВЫДАЧА НА АВТОКОДЕ'      ; 18 characters → exactly 3 cells
ТЕКСТ   Т'ОБ''ЕКТ'                 ; the 6 characters  ОБ"ЕКТ  → one cell
```

---

## 10. Basing Commands

As noted in [§7.5](#75-short-commands), the translator can sometimes choose an index register as a
**base** for a short command and compute the offset itself. To enable this, the programmer informs
the translator with `УПОТР` and `ОТМЕН`. (Compare [Madlen §15](Madlen.md#15-addressing-and-basing),
whose `BASE`/`BAS` mechanism serves the same purpose.)

### `УПОТР` — use a register as base

```
ignored          УПОТР    relocatable expression  (simple absolute expression)
```

Declares that an index register may be used for basing and tells the translator the **base address**
that register will hold at run time. The relocatable expression is the base value; the parenthesized
simple absolute expression is the register number. **`УПОТР` does not load the register** — that is
the programmer's responsibility.

```
ПРОГ1   СТАРТ   2000
        УИА     *(5)            ; load М[5] ← start address of ПРОГ1 at run time
        УПОТР   ПРОГ1(5)        ; tell the translator: М[5] holds ПРОГ1's start
```

If the base value changes, a new `УПОТР` must announce it. With АЛЬФА a relocatable label:

```
        УПОТР   АЛЬФА(7)        ; М[7] assumed to hold АЛЬФА
        ...
        УПОТР   АЛЬФА+4096(7)   ; from here, М[7] assumed to hold АЛЬФА+4096
```

### `ОТМЕН` — cancel a base register

```
ignored          ОТМЕН    (simple absolute expression)  (register number)
```

Stops a register from being used for basing. Ignored if the register was never named in a `УПОТР`;
if the value exceeds 15, the command is rejected with an error. A register cancelled by `ОТМЕН` can
be made available again with a later `УПОТР`.

---

## 11. Program-Linking Commands

Bemsh provides linkage so that one program can address objects defined in another. A "shared" object
may have its own internal name in each program; establishing the correspondence is a separate process
done by the loader. The two linking commands are `ВХОДН` and `ВНЕШН`.

### Terminology

Suppose program `ПРОГ1` defines a label `МАССИВ` for some memory area, and program `ПРОГ2` works with
that same area under its own internal name `ВЕКТОР`. Then:

- `МАССИВ` in `ПРОГ1`, where it is defined, is an **entry point** (входная точка).
- The same `МАССИВ` referenced in `ПРОГ2` is an **external label** (внешняя метка).
- `ВЕКТОР` in `ПРОГ2`, the internal equivalent of the external label, is a **free variable**
  (свободная переменная).

Collectively all of these are **linkage labels** (метки связи).

```
ПРОГ1   СТАРТ
        ВХОДН   МАССИВ              ; specify МАССИВ as an entry point
МАССИВ  ПАМ     (100)

ПРОГ2   СТАРТ
ВЕКТОР  ВНЕШН   ПРОГ1.МАССИВ        ; bind free variable ВЕКТОР to external МАССИВ
        СЛ      ВЕКТОР              ; …and use it like any other label
```

Note that `ПРОГ2` may have its *own* internal `МАССИВ`, and `ПРОГ1` its own `ВЕКТОР`, without
conflict.

### `ВХОДН` — specify an entry point

```
ignored          ВХОДН    label (entry point)
```

Specifies that the operand label — defined in this program — may supply its value to free variables
of other programs. Each entry point needs its own `ВХОДН`. The operand label must be defined
somewhere in the program (else an error). `ВХОДН` may appear before or after the defining statement.

### `ВНЕШН` — bind to an external label

```
label (free variable)    ВНЕШН    label.label  (foreign program . external label)
```

Performs two functions: it introduces (and specifies) a **free variable** — the name-field label —
and equates it to an **external label**. The operand is a pair of labels separated by a period: the
foreign program's name and the external label it specifies as an entry point.

`ВНЕШН` is the **only** place an external label may be written; everywhere else in the program the
corresponding free variable is used. The `ВНЕШН` describing a free variable must **precede** every
command that uses it as an operand.

Two abbreviated forms drop one of the two operand labels:

- `ПРОГ1.` (external label omitted) — the free variable gets the **start address** of the named
  foreign program.
- `.МАССИВ` (program name omitted) — the external label is searched in all programs in load order;
  the free variable gets the value of the first one found.

### Restrictions on free variables

1. A free variable may appear **only** in machine instructions and address constants.
2. In a machine instruction it may appear **only** in the address expression, not the index
   expression.
3. It may enter an expression **only as a positive addend**.
4. A free variable may **not** be an entry point.

A free variable may in principle be used in both long and short commands, but if its loaded value
does not fit a short address the program will not work; the translator flags every short command
that uses a free variable.

---

## 12. The Blankless Statement Form

To allow writing programs without the column form (бланк), Bemsh accepts a **blankless** form
(безбланковая форма) in which punctuation replaces spaces as field separators:

| Separator | Between |
|-----------|---------|
| `:` (colon) | name and operation |
| `,` (comma) | operation and operand |
| `;` (semicolon) | operand and comment |

Rules:

1. **Do not mix forms** within a statement (e.g. a colon after the name but a space before the
   operand).
2. The name must contain no spaces, and there must be no space between the name and its colon. The
   colon may not be omitted even when the name is absent — in that case it occupies the leftmost
   column.
3. Spaces may appear inside the operation and operand fields and are ignored, except when a space is
   a character of a text constant.
4. The total number of characters in a statement (including spaces) must not exceed **58** — the
   amount that fits on the form.

The translator chooses the form by which of two characters — space or colon — it meets first while
scanning the statement left to right. If a colon comes first, the translator switches to the
delimiter form, discards all spaces, and replaces the colon, the semicolon, and the leftmost comma
with spaces; the resulting statement is then translated by the ordinary rules.

---

## 13. Quick Reference

### Translator commands

| Mnemonic | Group | Meaning | Operand |
|----------|-------|---------|---------|
| `КОД`   | control | source encoding / dialect | encoding spec (name ignored) |
| `СТАРТ` | control | start of program | self-defining value = assumed start address |
| `ФИНИШ` | control | end of program | relocatable expression = entry point |
| `АДРЕС` | control | new address-counter value | relocatable expression |
| `СТРН`  | control | new listing page | — |
| `СТРОК` | control | skip listing lines | decimal number |
| `ЭКВ`   | defining | define a label | expression (predefined components) |
| `ПАМ`   | defining | reserve memory | cell count (self-defining value / label) |
| `КОНД`  | defining | long constant (whole cell) | `(p)T'c'` or `T(c)` |
| `КОНК`  | defining | short constant (half cell) | as `КОНД` (no `(p)`, no `Е`) |
| `ТЕКСТ` | defining | text constant | `Т'c'` |
| `УПОТР` | basing | use register as base | reloc. expr. `(abs. register)` |
| `ОТМЕН` | basing | cancel base register | `(abs. register)` |
| `ВХОДН` | linking | specify entry point | label |
| `ВНЕШН` | linking | bind external label | `prog.label` (name = free variable) |

### Constant types (`КОНД` / `КОНК`)

| Letter | Type | Body |
|--------|------|------|
| `Ф`/`F` | integer, decimal | `'…'` |
| `В` | integer, octal | `'…'` |
| `Х` | integer, hexadecimal | `'…'` |
| `Е` | floating-point (`КОНД` only) | `'mantissa[Eorder]'` |
| `А` | address (expression) | `(…)` |
| `Т` | text (`ТЕКСТ`) | `'…'` |

The constant body is in single quotes for all types except `А` (parentheses). `КОНД` allows a repeat
coefficient `(p)` for every type except `А`. Type `Е` is allowed only in `КОНД`.

### Machine mnemonics — alphabetical index

`ВЧ`=A-X · `ВЧАБ`=AMX · `ВЧОБ`=X-A · `ВЧП`=E-X · `ВЧПА`=E-N · `ВЫПР`=IJ ·
`ДЕЛ`=A/X · `ЗНАК`=AVX · `ЗП`=ATX · `ЗПМ`=STX · `И`=AAX · `ИЛИ`=AOX ·
`МОД`=WTC · `МОДА`=UTC · `НЕД`=ANX · `НТЖ`=AEX · `ПБ`=UJ · `ПВ`=VJM ·
`ПЕ`=U1A · `ПИНО`=V1M · `ПИО`=VZM · `ПО`=UZA · `РЕГ`=MOD · `РЖ`=XTR ·
`РЖА`=NTR · `РЗБ`=AUX · `СБР`=APX · `СД`=ASX · `СДА`=ASN · `СЛ`=A+X ·
`СЛИ`=J+M · `СЛИА`=UTM · `СЛП`=E+X · `СЛПА`=E+N · `СЛЦ`=ARX · `СТОП`=STOP ·
`СЧ`=XTA · `СЧИ`=ITA · `СЧИМ`=ITS · `СЧМ`=XTS · `СЧМР`=YTA · `СЧРЖ`=RTE ·
`УВВ`=EXT (priv.) · `УИ`=ATI · `УИА`=VTM · `УИИ`=MTJ · `УИМ`=STI ·
`УМН`=A*X · `ЧЕД`=ACX · `ЦИКЛ`=VLM

---

## 14. Assembling and Running Bemsh on Dubna (toolchain notes)

Sections 1–13 describe the *language* as defined in the 1967 manual. This section records the
practical conventions for feeding Bemsh to the modern **`besmc`** driver (which runs the native
*Макро-БЕМШ* translator inside the [`dubna`](Besm6_Unix_Assembler.md) simulator) and for running the
result. These are the rules this project's BESM-6 C backend (`genbesm --bemsh`,
[emit_bemsh.c](emit_bemsh.c)) obeys when it emits machine-generated Bemsh; several
were learned the hard way, by tracing wrong output on `dubna`, and are easy to get wrong by writing
straight from the manual.

### 14.1 The Macro-Bemsh input deck

`besmc` (and a `*bemsh` Dubna job) feed the source **verbatim** after the `*bemsh` control card and
add no framing of their own. Every module — one `СТАРТ … ФИНИШ` unit — must therefore be wrapped by
the source itself in a **Macro-Bemsh deck**:

```text
ввд$$$
        <one СТАРТ … ФИНИШ module>
квч$$$
трн$$$
0-0
блмак
бтмалф
кнц$$$
```

`ввд$$$` opens the input stream and `кнц$$$` closes the deck; the intervening `квч$$$ / трн$$$ /
0-0 / блмак / бтмалф` lines are the fixed translate/macro-library trailer the translator expects.
Treat the seven lines as an indivisible required wrapper.

**One module per deck.** The translator processes exactly **one** `СТАРТ … ФИНИШ` module between a
`ввд$$$` and its `кнц$$$`; a second `СТАРТ` inside the same deck is silently dropped. A translation
unit that defines several modules (e.g. a function plus its module-level globals, each of which is
its own `СТАРТ … ФИНИШ`) must emit **several decks back to back**. Multiple decks after a single
`*bemsh` card all assemble into one relocatable object, so a whole compiled `.c` file — or a whole
runtime library — is just a concatenation of one-module decks.

### 14.2 The `*bemsh` Dubna job

A minimal job that assembles a Bemsh program, links a prebuilt object library, and runs it:

```text
*name .
*disc:1/local
*file:LIB,40            ; attach the object library file LIB (basename LIB.bin) on slot 40
*library:40            ; search slot 40 to resolve externals at load time
*bemsh
<one or more ввд$$$ … кнц$$$ decks>
*main NAME             ; designate the entry object (see §14.3)
*execute
*end file
```

`*file:LIB,40` mounts a prebuilt library and `*library:40` makes the loader search it — both are
needed, and both go **before** `*bemsh`. `*main NAME` names the object to run and `*execute` loads
and starts it. A program's line output (via extracode `Э71`, the `b/tout` primitive) is bracketed in
the printed `.lst` between the second `≠` line and the trailing `----` separator — the same framing
as a Madlen job, so the two share one output scraper.

### 14.3 Entry point, externals, and calls

- **Auto entry.** `СТАРТ` names the program *and* makes that name an entry point automatically (no
  `ВХОДН` needed). `*main NAME` on the job then selects it. Because labels are truncated to six
  characters (§4), the name on the job must be the *truncated* form: a C `program` becomes `progra`,
  so the card reads `*main progra`.
- **`ФИНИШ` may be empty.** With `*main` selecting the entry, `ФИНИШ` needs no operand.
- **`СТАРТ` needs a start-address operand.** An empty operand is rejected; use `СТАРТ 1` (the value
  is irrelevant for a relocatable module — the loader relocates it).
- **Every call target needs an explicit `ВНЕШН`.** Unlike Madlen's `,call,` macro, Bemsh's `ПВ`
  (VJM) does **not** auto-declare its callee. Each distinct `ПВ` target must be declared external —
  `NAME ВНЕШН .NAME` (the search-all `.name` form) — before its first use, or the load leaves it
  undefined. (A recursive call to the current module's own `СТАРТ` name stays a local reference and
  must *not* be declared external.)
- **`ПВ` must carry the return register.** A call is `ПВ name(13)` — VJM through index register 13,
  which the callee reads to return via `ПБ (13)`. Writing a bare `ПВ name` puts the return link in
  register 0 (architecturally zero, so it is discarded) and the callee returns to a stale address —
  usually somewhere in the monitor. This is the single most common way to get a job that assembles
  cleanly (0 errors) yet runs into garbage.

### 14.4 Code labels: use a labeled instruction, not `ЭКВ *`

A branch target must be the address of a **whole cell** (control cannot enter the right instruction
of a cell — §7.7). Do **not** define a code label with `NAME ЭКВ *`: the `*` operand yields only the
*integer part* of the half-cell address counter (§5), so a label written after an odd number of
instructions captures the current (right-half) cell, while the instruction it is meant to name — the
next *labeled* item, which the translator forces into the **left half of a fresh cell** — lands one
cell later. A branch to such a label then executes the *previous* cell's left instruction instead.

Instead, define the label with `NAME НОП`. `НОП` is a Macro-Bemsh **label-holder** pseudo-operation:
it does *not* generate a machine instruction, but it aligns the address counter to a fresh whole cell
(padding any pending right half) and binds the name to that cell. The next real instruction is then
placed at that cell, so the label names it exactly, and a branch to the label lands on a genuine cell
boundary. (This is what `genbesm --bemsh` emits for every basic-block label.)

### 14.5 `ПАМ` does not clear memory

`ПАМ N` reserves *N* cells but does **not** zero them (§9.2) — a C-style tentative definition such as
`int counter;` compiled to `counter ПАМ 1` must not assume the cell is zero. In practice the Dubna
loader delivers a freshly loaded program region already zeroed, so simple cases work; but nothing in
the *language* guarantees it, and reused or overlaid memory will not be clear. Emit explicit zero
constants (`КОНД В'0'`, optionally with a repeat coefficient) when zero-initialization must be
guaranteed.

### 14.6 Reading the listing and tracing

- **Fields print in octal.** The `.lst` disassembly shows index registers and short addresses in
  octal. `СЧИМ 13` (decimal register 13) prints as `ITS 15`; a call `ПВ name(13)` prints as
  `VJM …(15)`. Octal `15` = decimal 13 — not a discrepancy.
- **Per-module summary.** Each module ends with a line `NAME HAM=… … BXOДH=… BHEШH=… ЧИCЛO METOK=…`
  (entry-point count, external count, label count) and `ЧИCЛO OШИБOK=NNNN` (error count). A nonzero
  error count, or an `OTCYTCTBYET NAME` at load time, pinpoints an undefined external.
- **Instruction tracing.** Re-run a job under the simulator with
  `dubna -d rime --trace=FILE job.dub` to get a full register/instruction/extracode/memory trace
  (modes: `r` registers, `i` instructions, `m` memory, `e` extracodes; add `p` for print extracodes).
  Follow a routine by its load address from the link map (the symbol/address table printed between
  the two `≠` lines), remembering that during loading the same low addresses are reused by the
  loader's own block-copy loop — the program proper runs last, near the end of the trace.

---

*This manual is an English description of the BESM-6 autocode (Bemsh) as defined in V. S. Shtarkman,
«Автокод для БЭСМ‑6. Описание языка (инструкция)», Institute of Applied Mathematics, USSR Academy of
Sciences, May 1967. For the machine itself see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md),
[Besm6_Data_Representation.md](Besm6_Data_Representation.md), and
[Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md); for the Latin-mnemonic autocode see
[Madlen.md](Madlen.md), and for the modern Unix assembler (Madlen mnemonics, AT&T-style syntax) see
[Besm6_Unix_Assembler.md](Besm6_Unix_Assembler.md).*
