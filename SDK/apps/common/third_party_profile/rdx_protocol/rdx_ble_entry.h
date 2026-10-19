#ifndef RDX_BLE_ENTRY_H
#define RDX_BLE_ENTRY_H
#include "generic/typedef.h"

/* 生命周期和广播操作在 app_core 串行执行；释放资源前先停止协调器。 */
void rdx_ble_entry_start(void);
void rdx_ble_entry_stop(void);

/* 事件发布接口不直接操作广播，也不恢复 PA4。 */
void rdx_ble_entry_request(u8 enable, u8 refresh);
void rdx_ble_entry_changed(void);
void rdx_ble_entry_topology_changed(void);
void rdx_ble_entry_activity(void);
void rdx_ble_entry_key(u8 down);
u8 rdx_ble_entry_needs_service(void);
void rdx_ble_entry_service(void);
u8 rdx_ble_entry_allowed(void);

/* app_core 适配接口；广播操作返回正数时，须等待明确的完成事件。 */
u8 rdx_ble_entry_suppressed(void);
u8 rdx_ble_entry_business_busy(void);
int rdx_ble_entry_radio_apply(u8 enable, u8 refresh);
u32 rdx_ble_entry_radio_deadline(void);

#endif
