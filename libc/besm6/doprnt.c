/*
 * Core formatting engine shared by printf / sprintf / snprintf.
 *
 * Derived from the FreeBSD kernel printf, heavily rewritten for the BESM-6 C
 * runtime.  Key adaptations:
 *
 *   - <stdarg.h> variadic access.  Arguments are read through a va_list (`ap`);
 *     every BESM-6 argument occupies exactly one word, so each va_arg advances
 *     by one word regardless of the argument type.
 *
 *   - The output device buffers KOI7 and folds letters to upper case.  Format
 *     conversion letters therefore arrive UPPER CASE ('%d' and '%D' both reach
 *     the engine as 'D'), and there is no lower-case output: %x/%X, %e/%E and
 *     %g/%G are indistinguishable and always print upper case.
 *
 *   - `long` == `int` and `double` == `float` (one word each), so the length
 *     modifiers 'L'/'H' are accepted and ignored.
 *
 * If `g_buf` is null the formatted bytes go to putbyte(); otherwise they are
 * stored into the caller buffer (at most g_size-1, NUL-terminated) for the
 * sprintf/snprintf family.  g_len always counts the total length that would be
 * produced, which is the return value.
 */
#include <math.h>
#include <stdio.h>

enum {
    MAXNBUF  = 32, /* digits buffer: 48-bit octal (16) + sign + prefix + slack  */
    MAX_DIG  = 12, /* max meaningful significant digits for a 48-bit double     */
    DEF_PREC = 6,  /* default precision for %f/%e and significant digits for %g */
};

/*
 * Output sink, set up by __doprnt before formatting.  The destination is chosen
 * by the integer flag g_to_buf rather than by testing g_buf for null: a null
 * char* is encoded as a fat pointer (marker bit set), so its truthiness is not a
 * reliable mode switch.
 */
static int g_to_buf; /* 1 = store into g_buf, 0 = emit via putbyte         */
static char *g_buf;  /* target buffer when g_to_buf                        */
static int g_size;   /* capacity of g_buf                                  */
static int g_len;    /* characters produced so far (the would-be length)   */

static void emit(int c)
{
    if (g_to_buf) {
        if (g_len < g_size - 1)
            g_buf[g_len] = (char)c;
    } else {
        putbyte(c);
    }
    ++g_len;
}

static void emit_pad(int c, int count)
{
    while (count > 0) {
        emit(c);
        --count;
    }
}

/* Map a value 0..15 to its upper-case hex digit. */
static int mkhex(int ch)
{
    ch = ch & 15;
    if (ch > 9)
        return ch + 'A' - 10;
    return ch + '0';
}

/*
 * Convert `ul` to base `base` digits, written into nbuf in reverse order.
 * nbuf[0] is a 0 sentinel; digits occupy nbuf[1..].  At least `prec` digits are
 * produced (zero padded).  Returns a pointer to the most-significant digit (so
 * the caller walks downward to nbuf+1 to print MSD..LSD) and stores the digit
 * count in *lenp.
 */
static char *ksprintn(char *nbuf, unsigned ul, int base, int prec, int *lenp)
{
    char *p = nbuf;

    *p = 0;
    do {
        *++p = (char)mkhex((int)(ul % (unsigned)base));
        ul   = ul / (unsigned)base;
    } while (--prec > 0 || ul);
    if (lenp)
        *lenp = (int)(p - nbuf);
    return p;
}

/*
 * Floating-point conversion (defined at the bottom of the file).  Works on the
 * caller buffer b[0..bsize-1], walking it with char* cursors.  Stores the
 * digits, sets *startidx to the index of the first character, and returns the
 * character count.  b[0] is reserved for a rounding carry.
 */
static int cvt(double number, int prec, int sharpflag, int *negp, int fmtch, char *b, int bsize,
               int *startidx);

int __doprnt(const char *fmt, va_list ap, char *buf, int size, int to_buf)
{
    char nbuf[MAXNBUF];
    int i, c, base, ladjust, sharpflag, neg, dot;
    int n, width, dwidth, sign, blank, extrazeros, padding, dlen;
    unsigned ul;
    char *s, *msd;

    g_to_buf = to_buf;
    g_buf    = buf;
    g_size   = size;
    g_len    = 0;

    i = 0;
    for (;;) {
        while ((c = fmt[i]) != '%') {
            if (!c)
                goto done;
            emit(c);
            ++i;
        }
        ++i;
        padding    = ' ';
        width      = 0;
        extrazeros = 0;
        ladjust    = 0;
        sharpflag  = 0;
        neg        = 0;
        sign       = 0;
        blank      = 0;
        dot        = 0;
        dwidth     = -1;

    reswitch:
        c = fmt[i];
        ++i;
        /*
         * Historically the KOI7 output device folded output (and hence the format
         * string) to upper case, so only '%D'-style upper-case conversion letters
         * ever reached the engine.  The Unix (b6as) path preserves the source case,
         * so '%d' now arrives lower case.  Fold the spec letter to upper case here
         * so both spellings — the conversion letters and the 'l'/'h' length
         * modifiers — behave identically.  Flags, width digits and '%' are not
         * letters and pass through unchanged.
         */
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (c == '.') {
            dot     = 1;
            padding = ' ';
            dwidth  = 0;
            goto reswitch;
        }
        if (c == '#') {
            sharpflag = 1;
            goto reswitch;
        }
        if (c == '+') {
            sign = -1;
            goto reswitch;
        }
        if (c == ' ') {
            blank = 1;
            goto reswitch;
        }
        if (c == '-') {
            ladjust = 1;
            goto reswitch;
        }
        if (c == 'L' || c == 'H') {
            /* length modifiers: long==int, short==int — accept and ignore */
            goto reswitch;
        }
        if (c == '*') {
            if (!dot) {
                width = va_arg(ap, int);
                if (width < 0) {
                    ladjust = 1;
                    width   = -width;
                }
            } else {
                dwidth = va_arg(ap, int);
            }
            goto reswitch;
        }
        if (c == '0' && !dot) {
            padding = '0';
            goto reswitch;
        }
        if (c >= '0' && c <= '9') {
            n = 0;
            for (;;) {
                n = n * 10 + c - '0';
                c = fmt[i];
                if (c < '0' || c > '9')
                    break;
                ++i;
            }
            if (dot)
                dwidth = n;
            else
                width = n;
            goto reswitch;
        }

        if (c == '%') {
            emit('%');
            continue;
        }

        if (c == 'C') {
            if (!ladjust)
                emit_pad(' ', width - 1);
            emit(va_arg(ap, int));
            if (ladjust)
                emit_pad(' ', width - 1);
            continue;
        }

        if (c == 'S') {
            /* Detect a null argument from the raw word, then reconstruct the
             * char* from it.  Reading it back through char** would re-decorate a
             * null word into a (nonzero) fat pointer and hide the null. */
            n = va_arg(ap, int);
            if (n == 0) {
                s = "(NULL)";
            } else {
                s = *(char **)&n;
            }
            if (!dot) {
                n = 0;
                while (s[n])
                    ++n;
            } else {
                n = 0;
                while (n < dwidth && s[n])
                    ++n;
            }
            width -= n;
            if (!ladjust)
                emit_pad(' ', width);
            for (c = 0; c < n; ++c)
                emit(s[c]);
            if (ladjust)
                emit_pad(' ', width);
            continue;
        }

        /* ---- floating point ---- */
        if (c == 'F' || c == 'E' || c == 'G') {
            double d;
            int sidx, slen;
            d = va_arg(ap, double);
            if (dwidth > MAX_DIG) {
                if (c != 'G' || sharpflag)
                    extrazeros = dwidth - MAX_DIG;
                dwidth = MAX_DIG;
            } else if (dwidth == -1) {
                dwidth = DEF_PREC;
            }
            if (d < 0) {
                neg = 1;
                d   = -d;
            }
            nbuf[0] = 0;
            slen    = cvt(d, dwidth, sharpflag, &neg, c, nbuf, MAXNBUF, &sidx);
            dlen    = slen + (neg ? 1 : 0);
            if (!ladjust && padding == ' ' && (width - dlen) > 0)
                emit_pad(' ', width - dlen);
            if (neg)
                emit('-');
            if (!ladjust && padding == '0' && (width - dlen) > 0)
                emit_pad('0', width - dlen);
            for (n = 0; n < slen; ++n) {
                if (extrazeros && nbuf[sidx + n] == 'E') {
                    emit_pad('0', extrazeros);
                    extrazeros = 0;
                }
                emit(nbuf[sidx + n]);
            }
            if (extrazeros)
                emit_pad('0', extrazeros);
            if (ladjust && (width - dlen) > 0)
                emit_pad(' ', width - dlen);
            continue;
        }

        /* ---- integer conversions ---- */
        if (c == 'D' || c == 'I') {
            ul = va_arg(ap, unsigned);
            if (!sign)
                sign = 1;
            base = 10;
            goto number;
        }
        if (c == 'U') {
            ul   = va_arg(ap, unsigned);
            base = 10;
            goto nosign;
        }
        if (c == 'O') {
            ul   = va_arg(ap, unsigned);
            base = 8;
            goto nosign;
        }
        if (c == 'X') {
            ul   = va_arg(ap, unsigned);
            base = 16;
            goto nosign;
        }
        if (c == 'P') {
            ul        = va_arg(ap, unsigned);
            base      = 16;
            sharpflag = 1;
            goto nosign;
        }

        /* unknown conversion: echo it verbatim */
        emit('%');
        emit(c);
        continue;

    nosign:
        sign  = 0;
        blank = 0;
    number:
        if (sign) {
            if ((int)ul < 0) {
                neg = '-';
                ul  = (unsigned)(-(int)ul);
            } else if (sign < 0) {
                neg = '+';
            } else if (blank) {
                neg = ' ';
            }
        }
        if (dwidth >= MAXNBUF) {
            extrazeros = dwidth - MAXNBUF + 1;
            dwidth     = MAXNBUF - 1;
        }
        msd = ksprintn(nbuf, ul, base, dwidth, &dlen);
        if (sharpflag && ul != 0) {
            if (base == 8)
                dlen += 1;
            else if (base == 16)
                dlen += 2;
        }
        if (neg)
            ++dlen;

        if (!ladjust && padding == ' ' && (width - dlen) > 0)
            emit_pad(' ', width - dlen);
        if (neg)
            emit(neg);
        if (sharpflag && ul != 0) {
            if (base == 8) {
                emit('0');
            } else if (base == 16) {
                emit('0');
                emit('X');
            }
        }
        if (extrazeros)
            emit_pad('0', extrazeros);
        if (!ladjust && padding == '0' && (width - dlen) > 0)
            emit_pad('0', width - dlen);
        while (msd > nbuf)
            emit(*msd--);
        if (ladjust && (width - dlen) > 0)
            emit_pad(' ', width - dlen);
        continue;
    }

done:
    if (g_to_buf && g_size > 0) {
        n = g_len;
        if (n > g_size - 1)
            n = g_size - 1;
        g_buf[n] = 0;
    }
    return g_len;
}

/*
 * Round the decimal digits start..end up by one unit in the last place,
 * propagating carries.  `expo` (when non-null) carries the e-format exponent so
 * a carry out of the leading digit bumps it; otherwise an f-format carry extends
 * left into the reserved slot and moves *startp back one.  Mirrors the FreeBSD
 * cvtround.
 */
static void cvtround(double fract, int *expo, char **startp, char *end, int ch, int *negp)
{
    double tmp;
    char *start, *p;
    int up;

    start = *startp;
    p     = end;

    if (fract) {
        modf(fract * 10, &tmp);
        up = (int)tmp;
    } else {
        up = ch - '0';
    }
    if (up > 4) {
        for (;; --p) {
            if (*p == '.')
                --p;
            ++*p;
            if (*p <= '9')
                break;
            *p = '0';
            if (p == start) {
                if (expo) { /* e/E: increment exponent */
                    *p = '1';
                    ++*expo;
                } else { /* f: prepend a digit into the reserved slot */
                    --p;
                    *p = '1';
                    --start;
                }
                break;
            }
        }
    } else if (*negp) {
        /* "%.3f" of -0.0004 must not print a negative zero */
        for (;; --p) {
            if (*p == '.')
                --p;
            if (*p != '0')
                break;
            if (p == start)
                *negp = 0;
        }
    }
    *startp = start;
}

/* Append the exponent suffix ("E+NN") at write cursor p; return the new cursor. */
static char *exponent(char *p, int expin, int fmtch)
{
    char eb[8];
    int expo, k;

    expo = expin;

    *p++ = (char)fmtch;
    if (expo < 0) {
        expo = -expo;
        *p++ = '-';
    } else {
        *p++ = '+';
    }
    k = 8;
    if (expo > 9) {
        do {
            --k;
            eb[k] = (char)(expo % 10 + '0');
            expo  = expo / 10;
        } while (expo > 9);
        --k;
        eb[k] = (char)(expo + '0');
        for (; k < 8; ++k)
            *p++ = eb[k];
    } else {
        *p++ = '0';
        *p++ = (char)(expo + '0');
    }
    return p;
}

/*
 * Format the magnitude `number` into b[] for %f/%e/%g.  Walks the buffer with
 * char* cursors.  b[0] is reserved for a rounding carry; formatting runs from
 * b+1.  *startidx receives the index of the first character (0 if a carry
 * extended left); the character count is returned.
 */
static int cvt(double number, int precin, int sharpflag, int *negp, int fmtch, char *b, int bsize,
               int *startidx)
{
    double fract, integer, tmp;
    char *p, *t, *start, *endp;
    int expcnt, gformat, dotrim, prec, ftc;

    prec    = precin;
    ftc     = fmtch;
    expcnt  = 0;
    gformat = 0;
    dotrim  = 0;
    fract   = modf(number, &integer);

    endp  = b + bsize - 1;
    start = b + 1; /* reserved rounding slot at b[0] */
    t     = b + 1;

    /* integer part, least-significant first, into the top of the buffer */
    p = endp - 1;
    while (integer) {
        tmp = modf(integer / 10, &integer);
        *p  = (char)((int)((tmp + 0.01) * 10) + '0');
        --p;
        ++expcnt;
    }

    if (fmtch == 'F') {
        if (expcnt) {
            ++p;
            while (p < endp)
                *t++ = *p++;
        } else {
            *t++ = '0';
        }
        if (prec || sharpflag)
            *t++ = '.';
        if (fract) {
            if (prec) {
                do {
                    fract = modf(fract * 10, &tmp);
                    *t++  = (char)((int)tmp + '0');
                } while (--prec && fract);
            }
            if (fract)
                cvtround(fract, 0, &start, t - 1, '0', negp);
        }
        for (; prec > 0; --prec)
            *t++ = '0';
        *startidx = (int)(start - b);
        return (int)(t - start);
    }

    if (fmtch == 'E') {
    eformat:
        if (expcnt) {
            ++p;
            *t++ = *p;
            if (prec || sharpflag)
                *t++ = '.';
            for (;;) {
                if (!prec)
                    break;
                ++p;
                if (p >= endp)
                    break;
                *t++ = *p;
                --prec;
            }
            if (!prec) {
                ++p;
                if (p < endp) {
                    fract = 0;
                    cvtround(0, &expcnt, &start, t - 1, *p, negp);
                }
            }
            --expcnt;
        } else if (fract) {
            for (expcnt = -1;; --expcnt) {
                fract = modf(fract * 10, &tmp);
                if (tmp)
                    break;
            }
            *t++ = (char)((int)tmp + '0');
            if (prec || sharpflag)
                *t++ = '.';
        } else {
            *t++ = '0';
            if (prec || sharpflag)
                *t++ = '.';
        }
        if (fract) {
            if (prec) {
                do {
                    fract = modf(fract * 10, &tmp);
                    *t++  = (char)((int)tmp + '0');
                } while (--prec && fract);
            }
            if (fract)
                cvtround(fract, &expcnt, &start, t - 1, '0', negp);
        }
        for (; prec > 0; --prec)
            *t++ = '0';
        if (gformat && !sharpflag) {
            while (t > start) {
                --t;
                if (*t != '0')
                    break;
            }
            if (*t == '.')
                --t;
            ++t;
        }
        t         = exponent(t, expcnt, ftc);
        *startidx = (int)(start - b);
        return (int)(t - start);
    }

    /* fmtch == 'G' */
    if (!prec)
        ++prec;
    if (expcnt > prec || (!expcnt && fract && fract < 0.0001)) {
        --prec;
        ftc     = 'E';
        gformat = 1;
        goto eformat;
    }
    if (expcnt) {
        for (;;) {
            ++p;
            if (p >= endp)
                break;
            *t++ = *p;
            --prec;
        }
    } else {
        *t++ = '0';
    }
    if (prec || sharpflag) {
        dotrim = 1;
        *t++   = '.';
    }
    while (prec && fract) {
        fract = modf(fract * 10, &tmp);
        *t++  = (char)((int)tmp + '0');
        --prec;
    }
    if (fract)
        cvtround(fract, 0, &start, t - 1, '0', negp);
    if (sharpflag) {
        for (; prec > 0; --prec)
            *t++ = '0';
    } else if (dotrim) {
        while (t > start) {
            --t;
            if (*t != '0')
                break;
        }
        if (*t != '.')
            ++t;
    }
    *startidx = (int)(start - b);
    return (int)(t - start);
}
