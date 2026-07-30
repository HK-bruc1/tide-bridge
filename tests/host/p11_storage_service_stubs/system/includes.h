#ifndef __P11_STORAGE_SERVICE_SYSTEM_INCLUDES_H__
#define __P11_STORAGE_SERVICE_SYSTEM_INCLUDES_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;

int p11_test_y_printf(const char *format, ...);
int p11_test_g_printf(const char *format, ...);

#define y_printf(...) p11_test_y_printf(__VA_ARGS__)
#define g_printf(...) p11_test_g_printf(__VA_ARGS__)

#endif
