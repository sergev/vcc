//
// va_arg for x86-64: the address of the next variadic argument of a type of `size`
// bytes, alignment `align` and System V class `cls` (__builtin_va_class), stepping
// `ap` past it — the algorithm of psABI §3.5.7.  An argument whose eightbytes do not
// all fit in the registers left, a MEMORY one and a long double come from the overflow
// area; a value of two eightbytes from registers is put together in `tmp`.
//
#include <stdarg.h>

enum { MEMORY = 0, INTEGER = 1, SSE = 2, X87 = 3 };

void *__va_arg(va_list ap, unsigned long size, unsigned long align, int cls, void *tmp)
{
    if (cls != MEMORY && cls != X87) {
        int n = cls >> 2 ? 2 : 1, need_gp = 0, need_fp = 0;
        for (int i = 0; i < n; i++) {
            need_gp += (cls >> 2 * i & 3) == INTEGER;
            need_fp += (cls >> 2 * i & 3) == SSE;
        }
        if (ap->gp_offset + 8 * need_gp <= 48 && ap->fp_offset + 16 * need_fp <= 176) {
            char *save = ap->reg_save_area;
            char *t    = tmp;
            for (int i = 0; i < n; i++) {
                char *p;
                if ((cls >> 2 * i & 3) == INTEGER) {
                    p = save + ap->gp_offset;
                    ap->gp_offset += 8;
                } else {
                    p = save + ap->fp_offset;
                    ap->fp_offset += 16;
                }
                if (n == 1)
                    return p;
                for (unsigned long j = 8 * i; j < size && j < 8 * i + 8; j++)
                    t[j] = p[j - 8 * i];
            }
            return tmp;
        }
    }
    char *p = ap->overflow_arg_area;
    if (align > 8)
        p = (char *)(((unsigned long)p + 15) & -16UL);
    ap->overflow_arg_area = p + (size + 7) / 8 * 8;
    return p;
}
