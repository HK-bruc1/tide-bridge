#ifndef UTILS_DEBUG_H
#define UTILS_DEBUG_H

/* Host mock replacement for SDK/utils/debug.h */
/* Maps JL log macros to printf for host tests. */

#include <stdio.h>

#define log_error(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define log_warn(fmt, ...)  printf(fmt "\n", ##__VA_ARGS__)
#define log_info(fmt, ...)  printf(fmt "\n", ##__VA_ARGS__)
#define log_debug(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

#endif
