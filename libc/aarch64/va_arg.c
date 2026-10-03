//
// va_arg for AArch64: the address of the next variadic argument of a type of `size`
// bytes, alignment `align` and AAPCS64 class `cls` (__builtin_va_class), stepping `ap`
// past it — the algorithm of AAPCS64 appendix B.4.  A class of 8 or more is a value
// in vector registers: `cls >> 3` bytes in each of `cls & 7` of them; an HFA of more
// than one is gathered into `tmp`.
//
#include <stdarg.h>

enum { GENERAL = 0, BY_REF = 1 };

void *__va_arg(va_list *ap, unsigned long size, unsigned long align, int cls, void *tmp)
{
    char *p;
    if (cls >= 8) {
        int count = cls & 7;
        int esize = cls >> 3;
        int offs  = ap->__vr_offs;
        if (offs < 0) {
            ap->__vr_offs = offs + 16 * count;
            if (ap->__vr_offs <= 0) {
                p = (char *)ap->__vr_top + offs;
                if (count == 1)
                    return p;
                char *t = tmp;
                for (int i = 0; i < count; i++)
                    for (int j = 0; j < esize; j++)
                        t[i * esize + j] = p[16 * i + j];
                return tmp;
            }
        }
    } else {
        int offs = ap->__gr_offs;
        if (offs < 0) {
            if (cls == GENERAL && align > 8)
                offs = (offs + 15) & -16;
            int n         = cls == BY_REF ? 1 : (int)((size + 7) / 8);
            ap->__gr_offs = offs + 8 * n;
            if (ap->__gr_offs <= 0) {
                p = (char *)ap->__gr_top + offs;
                return cls == BY_REF ? *(void **)p : p;
            }
        }
    }
    // On the stack.
    p = ap->__stack;
    if (cls != BY_REF && align > 8)
        p = (char *)(((unsigned long)p + 15) & -16UL);
    ap->__stack = p + (cls == BY_REF ? 8 : (size + 7) / 8 * 8);
    return cls == BY_REF ? *(void **)p : p;
}
