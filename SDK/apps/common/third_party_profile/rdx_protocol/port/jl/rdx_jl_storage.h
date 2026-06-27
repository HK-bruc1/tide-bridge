#ifndef __RDX_JL_STORAGE_H__
#define __RDX_JL_STORAGE_H__

#include "typedef.h"
#include "rdx_err.h"
#include "syscfg_id.h"

/*
 * VM ID 在此集中管理，业务代码不再直接写 syscfg id。
 * 枚举值映射到 syscfg_id.h 中实际定义的 VM ID 宏。
 *
 * 阶段 1 先定义当前主力组合 (Zenchord CC T2616) 使用的 VM ID；
 * 其他产品/板型的 VM ID 在后续阶段按 product config 逐步补齐。
 */

/* RDX 项目内的 VM ID — 来自 syscfg_id.h */
typedef enum {
    RDX_VM_ID_BLE_NAME          = VM_RDX_BLE_NAME,
    RDX_VM_ID_BLE_MAC           = VM_RDX_BLE_MAC,
    RDX_VM_ID_KEY_DUT_DISABLED  = VM_RDX_KEY_DUT_DISABLED,
    RDX_VM_ID_CHARGE_TIME_INFO  = VM_RDX_CHARGE_TIME_INFO,
    RDX_VM_ID_BOUND_STATUS      = VM_RDX_NOTTA_BOUND_STATUS,
    RDX_VM_ID_MIC_GAIN          = VM_RDX_MIC_GAIN,
    RDX_VM_ID_REC_ERR_REBOOT    = VM_RDX_REC_ERR_REBOOT,
    RDX_VM_ID_RTC_INIT_VALUE    = VM_RDX_RTC_INIT_VALUE,
    RDX_VM_ID_CUSTOM_AUTH       = VM_RDX_CUSTOM_AUTH,
} rdx_vm_id_t;

rdx_err_t rdx_storage_read(rdx_vm_id_t id, u8 *buf, u16 len);
rdx_err_t rdx_storage_write(rdx_vm_id_t id, const u8 *buf, u16 len);

/* thin wrappers around syscfg_read_string / syscfg_write */
rdx_err_t rdx_storage_cfg_read_string(u16 id, void *buf, u16 len, u8 ver);
rdx_err_t rdx_storage_cfg_write(u16 id, const void *buf, u16 len);

#endif
