#ifndef RDX_ADV_POLICY_H
#define RDX_ADV_POLICY_H
#include "generic/typedef.h"

/* 活动状态同步发布；BLE 操作在 app_core 上串行执行。 */
void rdx_adv_policy_activity(void);
void rdx_adv_policy_changed(void);
void rdx_adv_policy_enable(void);
void rdx_adv_policy_disable(void);
int rdx_adv_policy_business_enter(void);
void rdx_adv_policy_business_exit(void);
u8 rdx_adv_policy_idle_valid(void); /* 可在中断上下文安全调用，不查询 SDK 状态。 */

/* 产品适配接口，仅由策略消费端调用。 */
u32 rdx_adv_policy_hold_snapshot(void);
u8 rdx_adv_policy_allowed(void);
int rdx_adv_policy_radio_set(u8 slow);
void rdx_adv_policy_slow_committed(void);
#endif
