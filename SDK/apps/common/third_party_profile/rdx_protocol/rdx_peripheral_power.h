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

/*
 * PA4 is active high and feeds the shared SD NAND + RGB rail. Early init and
 * ensure_on are synchronous and may be used before RDX/BLE tasks exist. They
 * only guarantee the electrical rail; a future OFF->ON path must separately
 * gate storage access until driver and filesystem recovery completes.
 */
void rdx_peripheral_power_vdd_early_init(void);
int rdx_peripheral_power_vdd_ensure_on(rdx_shared_vdd_wake_reason_t reason);

/* Runtime notifications are serialized onto app_core by the manager. */
int rdx_peripheral_power_vdd_fast_adv_notify(void);
void rdx_peripheral_power_vdd_slow_adv_notify(void);
void rdx_peripheral_power_vdd_ble_links_changed_notify(void);
void rdx_peripheral_power_vdd_business_changed_notify(void);

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
