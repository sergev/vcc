/*
 * libc/common/float32.c compiled for the host, its names prefixed so that they do not
 * stand in for the host's own runtime: float32_tests.cpp checks it against the host's
 * float, bit for bit.
 */
#define __addsf3      f32_addsf3
#define __subsf3      f32_subsf3
#define __mulsf3      f32_mulsf3
#define __divsf3      f32_divsf3
#define sqrtf         f32_sqrtf
#define __eqsf2       f32_eqsf2
#define __nesf2       f32_nesf2
#define __ltsf2       f32_ltsf2
#define __lesf2       f32_lesf2
#define __gtsf2       f32_gtsf2
#define __gesf2       f32_gesf2
#define __unordsf2    f32_unordsf2
#define __fixsfsi     f32_fixsfsi
#define __fixunssfsi  f32_fixunssfsi
#define __floatsisf   f32_floatsisf
#define __floatunsisf f32_floatunsisf
#include "../../../libc/common/float32.c"
