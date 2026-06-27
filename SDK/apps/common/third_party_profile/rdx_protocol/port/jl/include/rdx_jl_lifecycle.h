#ifndef __RDX_JL_LIFECYCLE_H__
#define __RDX_JL_LIFECYCLE_H__

#include "typedef.h"

/* 芯片早期初始化，在 main 入口调用 */
void rdx_jl_early_init(void);

/* 进入低功耗前调用，返回 0 允许 sleep，非 0 阻止 */
int  rdx_jl_pre_sleep(void);

/* 唤醒后调用 */
void rdx_jl_post_wakeup(void);

/* 软关机前调用 */
void rdx_jl_pre_poweroff(void);

#endif
