#ifndef __RDX_CLOCK_SERVICE_H__
#define __RDX_CLOCK_SERVICE_H__

#include "typedef.h"

bool rdx_clock_service_is_locked(void);
void rdx_clock_service_lock(const char *task_name, int clk);
void rdx_clock_service_unlock(const char *task_name);
void rdx_clock_service_lock_with_timer(const char *task_name, int clk);
void rdx_clock_service_unlock_with_timer(const char *task_name);

#endif
