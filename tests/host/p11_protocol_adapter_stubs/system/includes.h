#ifndef __P11_TEST_SYSTEM_INCLUDES_H__
#define __P11_TEST_SYSTEM_INCLUDES_H__

#include <stddef.h>
#include <string.h>

void p11_test_critical_enter(void);
void p11_test_critical_exit(void);

#define CPU_CRITICAL_ENTER() p11_test_critical_enter()
#define CPU_CRITICAL_EXIT() p11_test_critical_exit()

#endif
