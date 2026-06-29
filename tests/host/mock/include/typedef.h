#ifndef TYPEDEF_H
#define TYPEDEF_H

/* Host mock replacement for SDK/interface/system/generic/typedef.h */

#include <stdint.h>

/* Provide stdbool keywords; tcc does not expose <stdbool.h> reliably */
#ifndef __bool_true_false_are_defined
#define bool  int
#define true  1
#define false 0
#define __bool_true_false_are_defined 1
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef SIZEOF
#define SIZEOF(x) (sizeof(x))
#endif

#endif
