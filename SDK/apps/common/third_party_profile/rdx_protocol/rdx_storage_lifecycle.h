#ifndef RDX_STORAGE_LIFECYCLE_H
#define RDX_STORAGE_LIFECYCLE_H

#include "app_config.h"

/* 仅负责文件服务准入与 PC 归还存储后的索引恢复。
 * 存储错误不得阻止 BLE、HOGP 或原生关机。 */
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
