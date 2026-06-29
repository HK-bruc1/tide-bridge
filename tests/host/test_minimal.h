#ifndef TEST_MINIMAL_H
#define TEST_MINIMAL_H

#include <stdio.h>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s:%d: (%s)\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define TEST_RUN(name) do { \
    printf("  subtest: %s\n", #name); \
    int _ret = name(); \
    if (_ret) return _ret; \
} while (0)

#define TEST_PASS() do { \
    printf("PASS: %s\n", __func__); \
    return 0; \
} while (0)

#endif
