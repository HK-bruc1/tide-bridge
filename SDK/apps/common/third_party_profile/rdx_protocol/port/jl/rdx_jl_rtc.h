#ifndef __RDX_JL_RTC_H__
#define __RDX_JL_RTC_H__

#include "typedef.h"
#include "rdx_err.h"

/*
 * 阶段 1 只定义接口签名。
 * 最小实现可保留原有 rdx_rtc.c 逻辑不变；
 * 阶段 3 多芯片适配时再按 chip family 拆分实现。
 */

rdx_err_t rdx_rtc_set_time(u32 timestamp);
u32       rdx_rtc_get_time(void);

#endif
