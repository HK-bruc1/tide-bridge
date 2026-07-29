#ifndef __P11_STORAGE_DOMAIN_SYSTEM_INCLUDES_H__
#define __P11_STORAGE_DOMAIN_SYSTEM_INCLUDES_H__

int p11_test_y_printf(const char *format, ...);

#define y_printf(...) p11_test_y_printf(__VA_ARGS__)

#endif
