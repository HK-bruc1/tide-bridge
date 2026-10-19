#ifndef RDX_PERIPHERAL_POWER_H
#define RDX_PERIPHERAL_POWER_H

#include "generic/typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RDX_SHARED_VDD_STATE_ON_READY = 0,
    RDX_SHARED_VDD_STATE_STOPPING,
    RDX_SHARED_VDD_STATE_QUIESCED_ON,
    RDX_SHARED_VDD_STATE_OFF,
    RDX_SHARED_VDD_STATE_STARTING,
    RDX_SHARED_VDD_STATE_FAULT_ON,
} rdx_shared_vdd_state_t;

typedef enum {
    RDX_SHARED_VDD_OWNER_DEVICE = 0,
    RDX_SHARED_VDD_OWNER_IDLE_BLOCKED,
    RDX_SHARED_VDD_OWNER_USB_HOST,
    RDX_SHARED_VDD_OWNER_RECOVERY,
} rdx_shared_vdd_owner_t;

enum {
    RDX_SHARED_VDD_BUSY_BLE_LINK        = (1u << 0),
    RDX_SHARED_VDD_BUSY_RECORD          = (1u << 1),
    RDX_SHARED_VDD_BUSY_PLAYBACK        = (1u << 2),
    RDX_SHARED_VDD_BUSY_FILE_OP         = (1u << 3),
    RDX_SHARED_VDD_BUSY_USB_MSC         = (1u << 4),
    RDX_SHARED_VDD_BUSY_FORMAT_RECOVERY = (1u << 5),
    RDX_SHARED_VDD_BUSY_RGB_REQUIRED    = (1u << 6),
    /* 以下忙位只用于连接态资格 */
    RDX_SHARED_VDD_BUSY_EXTERNAL_POWER  = (1u << 7),
    RDX_SHARED_VDD_BUSY_BUSINESS        = (1u << 8),
    RDX_SHARED_VDD_BUSY_STORAGE_IO      = (1u << 9),
    RDX_SHARED_VDD_BUSY_STORAGE_STATE   = (1u << 10),
    RDX_SHARED_VDD_BUSY_SHUTDOWN        = (1u << 11),
};

typedef enum {
    RDX_SHARED_VDD_WAKE_FAST_ADV = 0,
    RDX_SHARED_VDD_WAKE_BLE_LINK,
    RDX_SHARED_VDD_WAKE_SD_DRIVER,
    RDX_SHARED_VDD_WAKE_BUSINESS,
    RDX_SHARED_VDD_WAKE_USB_MSC,
} rdx_shared_vdd_wake_reason_t;

/* PE5 is active high. This is the only runtime write entry for the amplifier. */
void rdx_peripheral_power_amp_set(u8 enable);
u8 rdx_peripheral_power_amp_is_enabled(void);

/* PA4 高有效，给 SD NAND/RGB 共同供电。early_init 负责早期上电；
 * ensure_on 同步恢复电源、SD 挂载及 RGB，失败时禁止业务继续访问。
 * SD_DRIVER 原生回调仅保证电气供电，不递归进入挂载流程。 */
void rdx_peripheral_power_vdd_early_init(void);
int rdx_peripheral_power_vdd_ensure_on(rdx_shared_vdd_wake_reason_t reason);

/* Runtime notifications are serialized onto app_core by the manager. */
int rdx_peripheral_power_vdd_fast_adv_notify(void);
void rdx_peripheral_power_vdd_slow_adv_notify(void);
void rdx_peripheral_power_vdd_ble_links_changed_notify(void);
void rdx_peripheral_power_vdd_business_changed_notify(void);
/* 仅通知连接态评估，不递归发布广播策略事件。 */
void rdx_peripheral_power_vdd_connected_changed_notify(void);
/* 业务占用或存储 IO 用户归零时调用；仅在连接态评估等待时投递一次事件 */
void rdx_peripheral_power_vdd_idle_hint(void);

/* PC storage reserves the rail before taking SD0 over, then publishes owner. */
int rdx_peripheral_power_vdd_usb_prepare(void);
void rdx_peripheral_power_vdd_usb_takeover_complete(u8 success);
void rdx_peripheral_power_vdd_usb_restore_complete(u8 success);

rdx_shared_vdd_state_t rdx_peripheral_power_vdd_state_get(void);
rdx_shared_vdd_owner_t rdx_peripheral_power_vdd_owner_get(void);
u8 rdx_peripheral_power_vdd_is_enabled(void);
u8 rdx_peripheral_power_vdd_is_ready(void);
u8 rdx_peripheral_power_vdd_is_slow_adv(void);
u32 rdx_peripheral_power_vdd_busy_mask_get(void);
u32 rdx_peripheral_power_vdd_generation_get(void);

#ifdef __cplusplus
}
#endif

#endif /* RDX_PERIPHERAL_POWER_H */
