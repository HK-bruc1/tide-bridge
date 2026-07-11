/*=====================================================================================
 HEADER NAME: rdx_hogp_keyboard.h
 MODULE NAME: RDX BLE HID-over-GATT keyboard submodule public API.

 PRE-INCLUDE FILES DESCRIPTION:

 GENERAL DESCRIPTION:
    Public API for the HOGP keyboard capability. HOGP is implemented as a
    submodule of the RDX GATT Server and reuses the single app_ble wrapper
    handle allocated by RDX.
 =======================================================================================*/

#ifndef _RDX_HOGP_KEYBOARD_H_
#define _RDX_HOGP_KEYBOARD_H_

#include "system/includes.h"
#include "btstack/btstack_typedef.h"
#include "ble_user.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
* Lifecycle
******************************************************************************/
void rdx_hogp_init(void *app_ble_hdl);
void rdx_hogp_deinit(void);
void rdx_hogp_runtime_cleanup(void);

/******************************************************************************
* Diagnostics
******************************************************************************/
void rdx_hogp_dump_state(void);

/******************************************************************************
* Mode
******************************************************************************/
u8   rdx_hogp_mode_get(void);
void rdx_hogp_mode_set(u8 enable);

/******************************************************************************
* ATT routing
******************************************************************************/
u8   rdx_hogp_is_handle(u16 att_handle);

u16  rdx_hogp_att_read(hci_con_handle_t connection_handle,
                        u16 att_handle,
                        u16 offset,
                        u8 *buffer,
                        u16 buffer_size);

int  rdx_hogp_att_write(hci_con_handle_t connection_handle,
                         u16 att_handle,
                         u16 transaction_mode,
                         u16 offset,
                         u8 *buffer,
                         u16 buffer_size);

/******************************************************************************
* Connection / security events
******************************************************************************/
void rdx_hogp_on_connected(u16 con_handle);
void rdx_hogp_on_disconnected(u16 con_handle);
void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status);
void rdx_hogp_on_sm_event(u8 packet_type, u8 *packet, u16 size);

/******************************************************************************
* Advertising
******************************************************************************/
int  rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len);
void rdx_hogp_adv_start(void);
void rdx_hogp_adv_stop(void);

/******************************************************************************
* Keyboard Report API
******************************************************************************/
#define RDX_HOGP_KEYBOARD_REPORT_LEN  8

typedef struct {
    u8 modifiers;
    u8 reserved;
    u8 usages[6];
} rdx_hogp_keyboard_report_t;

int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report);
int rdx_hogp_keyboard_release_all(void);
u8  rdx_hogp_keyboard_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_KEYBOARD_H_ */
