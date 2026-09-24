#ifndef RDX_STORAGE_LIFECYCLE_H
#define RDX_STORAGE_LIFECYCLE_H

#include "app_config.h"

/* 状态机服务和 PC 归还通知由 app_core 调用，业务准入检查同时保护工作任务。
 * 即使关闭 USB 导出，也必须保留存储安全保护。 */
#if TCFG_DIP_SWITCH_POWER_ENABLE
int rdx_storage_lifecycle_service(int on, int vbus);
int rdx_storage_lifecycle_business_blocked(void);
int rdx_storage_lifecycle_transition_led(void);
int rdx_storage_lifecycle_shutdown_deferred(void);
void rdx_storage_lifecycle_pc_returned(void);
#else
static inline int rdx_storage_lifecycle_service(int on, int vbus) { return 0; }
static inline int rdx_storage_lifecycle_business_blocked(void) { return 0; }
static inline int rdx_storage_lifecycle_transition_led(void) { return 0; }
static inline int rdx_storage_lifecycle_shutdown_deferred(void) { return 0; }
static inline void rdx_storage_lifecycle_pc_returned(void) {}
#endif

#endif
