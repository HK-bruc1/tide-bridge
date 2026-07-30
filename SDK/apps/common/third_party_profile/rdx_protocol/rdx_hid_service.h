#ifndef _RDX_HID_SERVICE_H_
#define _RDX_HID_SERVICE_H_

#include "system/includes.h"
#include "btstack/btstack_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RDX_HID_REPORT_KEYBOARD = 0x01,
    RDX_HID_REPORT_CODEX = 0x06,
} rdx_hid_report_id_t;

void rdx_hid_service_init(void *app_ble_hdl);
void rdx_hid_service_deinit(void);

u16 rdx_hid_service_att_read(hci_con_handle_t connection_handle,
                             u16 att_handle, u16 offset,
                             u8 *buffer, u16 buffer_size);
int rdx_hid_service_att_write(hci_con_handle_t connection_handle,
                              u16 att_handle, u16 transaction_mode,
                              u16 offset, u8 *buffer, u16 buffer_size);

void rdx_hid_service_on_connected_with_hdl(void *app_ble_hdl,
                                           u16 con_handle, u8 encrypted);
void rdx_hid_service_on_connected(u16 con_handle, u8 encrypted);
void rdx_hid_service_on_disconnected(u16 con_handle);
void rdx_hid_service_on_encryption_change(u16 con_handle, u8 enabled,
                                          u8 status);
void rdx_hid_service_on_sm_event(u8 packet_type, u8 *packet, u16 size);
u8 rdx_hid_service_peer_has_persisted_subscription(u16 con_handle);

u8 rdx_hid_service_is_connected(void);
u8 rdx_hid_service_route_is_active(void);
u8 rdx_hid_report_is_ready(rdx_hid_report_id_t report_id);
int rdx_hid_report_notify(rdx_hid_report_id_t report_id,
                          u16 value_handle, u8 *data, u16 len);
void rdx_hid_service_dump_state(void);

#ifdef __cplusplus
}
#endif

#endif
