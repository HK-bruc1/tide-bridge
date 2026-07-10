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
#include "rdx_hogp_config.h"

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
* Key input
******************************************************************************/
int  rdx_hogp_key_send_usage(u8 usage, u8 pressed);
int  rdx_hogp_key_click_usage(u8 usage);
int  rdx_hogp_key_click_index(u8 key_index);
int  rdx_hogp_on_io_num_key(u8 num_idx, u8 action);

/******************************************************************************
* Legacy compatibility wrappers (remove once rdx_app.c migrates to new API)
******************************************************************************/
#if TCFG_RDX_HOGP_ENABLE
void hogp_mode_set(u8 enable);
u8   hogp_mode_get(void);
void hogp_key_send(u8 key_index, u8 pressed);
void hogp_key_click_send(u8 key_index);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _RDX_HOGP_KEYBOARD_H_ */
