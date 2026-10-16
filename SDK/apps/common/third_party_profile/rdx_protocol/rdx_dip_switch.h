#ifndef __RDX_DIP_SWITCH_H__
#define __RDX_DIP_SWITCH_H__

#include "app_config.h"
#include "rdx_commonDef.h"
#include "gpio.h"
#include "power/power_wakeup.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE

/* P33 只记录边沿；由独立保护完成消抖，不在此上下文投递任务。 */
void rdx_dip_switch_p33_irq(P33_IO_WKUP_EDGE edge);

/* 设备管理初始化后建立保护与事件桥接，不启动常驻系统轮询。 */
void rdx_dip_switch_init(void);
/* 合并状态变化通知；允许普通任务和中断调用，不在调用者执行协调。 */
void rdx_dip_switch_wake(void);
int rdx_dip_switch_pc_allowed(void);
int rdx_dip_switch_cold_service(void);
void rdx_dip_switch_note_business_mode(void);
void rdx_dip_switch_shutdown_begin(void);
int rdx_dip_switch_shutdown_pending(void);

#else
static inline void rdx_dip_switch_wake(void) {}
#endif // TCFG_DIP_SWITCH_POWER_ENABLE

#endif // __RDX_DIP_SWITCH_H__
