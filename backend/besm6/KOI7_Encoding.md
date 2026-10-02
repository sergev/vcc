# KOI-7 Encoding

The BESM-6 has no notion of ASCII or Unicode. Its character set is **KOI-7**
(КОИ-7, *Код Обмена Информацией, 7-бит* — "7-bit information interchange code", a
Soviet GOST 13052-67 standard with several national variants Н0/Н1/Н2). The
BESM-6/Dubna line printer renders the variant documented here, which is essentially
**KOI-7 Н2**: it carries both the uppercase Latin alphabet and the uppercase Russian
Cyrillic alphabet in a single 7-bit (0..127) code space, plus a block of mathematical
and typographic symbols in the low (control) region.

Because the target machine speaks KOI-7, the compiler re-encodes every string and
character literal from the host's UTF-8 source representation into KOI-7 when it emits
static data. This document describes that encoding and tabulates the conversion the
code generator performs.

## Where the conversion lives in the compiler

The conversion is implemented by `utf8_to_koi7()` in
[backend/besm6/utf8_to_koi7.c](utf8_to_koi7.c). It decodes each UTF-8
sequence to a Unicode code point and maps it through `unicode_to_koi7()`:

- The ASCII range (`U+0000`..`U+00FF`) goes through the `tab0[256]` lookup table.
- Cyrillic (`U+0400`..`U+04FF`) and assorted math/typographic code points
  (`U+2000`..`U+25FF`) are handled by `switch` arms.
- Anything unmapped becomes `0` (a NUL byte, which the printer ignores).

It is invoked from [backend/besm6/static.c](static.c) when emitting
string constants (`string_constant_log_items`) and `char` array initializers
(`char_init_item_bytes`, `char_array_log_items`). The resulting KOI-7 bytes are packed
six-to-a-word into 48-bit BESM-6 words and emitted as `,log,` data.

Two behaviors are worth highlighting up front:

- **Case folding.** Lowercase Latin `a`..`z` is folded to uppercase `A`..`Z` — KOI-7
  has no lowercase Latin letters. (Source string `"abc"` is stored as `ABC`.)
- **Cyrillic → Latin look-alikes.** Cyrillic letters whose glyphs coincide with a
  Latin letter are mapped onto that Latin code (Cyrillic А→`A`, В→`B`, Е→`E`, etc.);
  the Cyrillic-only letters occupy codes `0x60`..`0x7E`.

## The KOI-7 code page

The table below is the empirical glyph for every code 0..127, collected by running a
test program on the Dubna simulator (see *Reproducing the data* below). Columns are the
high nibble (rows) and low nibble (columns) of the code.

|      | _0 | _1 | _2 | _3 | _4 | _5 | _6 | _7 | _8 | _9 | _A | _B | _C | _D | _E | _F |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| **0_** | ␀ |   |   |   |   | Ъ | × |   |   | ␉ | ␊ |   |   |   | ≤ | ≥ |
| **1_** | ‘ |   |   |   |   | ― | ↑ | ⏨ | ≠ | ° | ÷ | ’ | ⊃ | ≡ | ∨ | ¬ |
| **2_** | ␠ | ! | " | # | $ | % | & | ' | ( | ) | * | + | , | - | . | / |
| **3_** | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | : | ; | < | = | > | ? |
| **4_** | @ | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O |
| **5_** | P | Q | R | S | T | U | V | W | X | Y | Z | [ | ‾ | ] | \| | _ |
| **6_** | Ю | А | Б | Ц | Д | Е | Ф | Г | Х | И | Й | К | Л | М | Н | О |
| **7_** | П | Я | Р | С | Т | У | Ж | В | Ь | Ы | З | Ш | Э | Щ | Ч |   |

Notes:

- `␀` (code `000`) and code `177` (DEL) render as nothing; several low codes
  (`001`–`004`, `006`, `013`–`014`, `021`–`024`, …) are blank/undefined on the printer.
- `␉` (`011`) is a horizontal tab and `␊` (`012`) a line feed — the only two control
  codes with a printer action; `␠` (`040`) is the space.
- The low region (`005`..`037`) holds graphic symbols, not C0 control characters:
  `Ъ ×`, `≤ ≥`, the quotes `‘ ’`, the rule `―`, `↑`, the decimal-exponent mark `⏨`,
  `≠ ° ÷`, `⊃ ≡ ∨ ¬`.
- `0x5C` is an **overline `‾`** (not a backslash) and `0x5E` is a **vertical bar `|`**
  (not a caret).
- Codes `0x60`..`0x7E` are the Cyrillic-only uppercase letters. Note the arrangement is
  *not* Russian-alphabetical: the letters that look like Latin live in the `0x41`..`0x5A`
  region, and only the distinct shapes are gathered here.

## ASCII → KOI-7 conversion (the `tab0` table)

For ASCII codes `0x00`..`0x5B` and the two codes `0x5D` (`]`) and `0x5F` (`_`), the
conversion is the **identity** — the byte is stored unchanged and renders as the same
glyph (because KOI-7 agrees with ASCII across `0x20`..`0x5B`). All other ASCII codes are
remapped; the complete list of non-identity mappings is:

| ASCII char | ASCII | KOI-7 (oct) | KOI-7 (hex) | Renders as | Note |
|------------|-------|-------------|-------------|------------|------|
| `\`        | 0x5C  | `035`       | 0x1D        | `≡`        | backslash → identical-to sign |
| `^`        | 0x5E  | `134`       | 0x5C        | `‾`        | caret → overline |
| `` ` ``    | 0x60  | `000`       | 0x00        | *(dropped)*| no KOI-7 equivalent |
| `a`..`z`   | 0x61..0x7A | `101`..`132` | 0x41..0x5A | `A`..`Z` | case-folded to uppercase |
| `{`        | 0x7B  | `016`       | 0x0E        | `≤`        | |
| `\|`       | 0x7C  | `136`       | 0x5E        | `\|`       | vertical bar preserved |
| `}`        | 0x7D  | `017`       | 0x0F        | `≥`        | |
| `~`        | 0x7E  | `037`       | 0x1F        | `¬`        | tilde → not sign |
| DEL        | 0x7F  | `000`       | 0x00        | *(dropped)*| |

Everything else passes through unchanged: digits, the space, the punctuation
`! " # $ % & ' ( ) * + , - . / : ; < = > ? @`, the brackets `[ ]`, and the uppercase
Latin letters `A`..`Z`. So the practical caveats when authoring a string literal for the
BESM-6 are: use **uppercase** Latin (lowercase is folded), and avoid `\` `^` `` ` `` `{`
`}` `~` unless you actually want the symbol shown in the table above.

## Extended (non-ASCII) mappings

`unicode_to_koi7()` also accepts a useful subset of Unicode so that source written in
UTF-8 can name KOI-7 glyphs directly:

- **Cyrillic** `U+0400`..`U+04FF` (Russian and a few Ukrainian letters): upper- and
  lowercase both map to the single uppercase KOI-7 code — Latin look-alikes onto
  `0x41`..`0x5A`, the rest onto `0x60`..`0x7E` (e.g. Б→`0x62`, Ж→`0x76`, Я→`0x71`).
- **Typography / math** `U+2000`..`U+25FF`: e.g. `≠`→`0x18`, `≡`→`0x1D`, `≤`→`0x0E`,
  `≥`→`0x0F`, `⊃`→`0x1C`, `∨`→`0x1E`, `∧`→`&`, `―`→`0x15`, `↑`→`0x16`, `⏨`→`0x17`,
  the directional quotes `‘`→`0x10` / `’`→`0x1B`.

See the `switch` arms in [utf8_to_koi7.c](utf8_to_koi7.c) for the full
list. Any code point not covered maps to `0` and is dropped.

## Reproducing the data

The glyph column of the code-page table was obtained by compiling and running this
program through our own toolchain on the Dubna simulator:

```c
#include <stdio.h>
void program() {
    int i;
    for (i = 0; i < 128; i++) {
        printf("%d:", i);
        printf("%c", i);   // emit raw byte i — the printer renders glyph(i)
        printf(":\n");
    }
}
```

`printf("%c", i)` emits the byte value `i` directly (the compile-time KOI-7 conversion
applies only to *literals*, not to runtime values), so the simulator's printer renders
`glyph(i)` for each code. Build and run it with:

```sh
cd <repo>
cc -E -nostdinc -Ilibc/besm6/include -Ilibc/common/include koi7probe.c > koi7probe.i
./build/parse        koi7probe.i   koi7probe.ast
./build/lower -t besm6 koi7probe.ast koi7probe.tac
./build/backend/genbesm koi7probe.tac koi7probe.mad

# Wrap koi7probe.mad in the standard job boilerplate (see any build/backend/besm6/*.dub):
#   *name . / *disc:1/local / *file:libc,40 / *call setftn:one,long / *assem
#   <contents of koi7probe.mad>
#   *library:40 / *execute / *end file

cd build/backend/besm6          # run here so libc.bin is linked
dubna koi7probe.dub > koi7probe.lst
```

The Dubna printer listing decodes the emitted KOI-7 bytes back to Unicode, so each line
of `koi7probe.lst` shows `<i>:<glyph>:` and reveals the glyph for code `i`.

## See also

- [backend/besm6/Besm6_Data_Representation.md](Besm6_Data_Representation.md) — scalar type layouts,
  including how `char` packs into 48-bit words.
- [backend/besm6/Madlen.md](Madlen.md) — Madlen assembler syntax and six-characters-per-word text
  constants.
- [КОИ-7 on Wikipedia](https://ru.wikipedia.org/wiki/КОИ-7) — the reference cited in the
  conversion source.
