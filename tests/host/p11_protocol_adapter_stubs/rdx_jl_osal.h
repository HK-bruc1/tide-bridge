#ifndef __P11_TEST_RDX_JL_OSAL_H__
#define __P11_TEST_RDX_JL_OSAL_H__

#include "rdx_err.h"

rdx_err_t rdx_os_task_post_callback2(const char *task_name,
                                     void (*callback)(void *, void *),
                                     void *arg1,
                                     void *arg2);

#endif
