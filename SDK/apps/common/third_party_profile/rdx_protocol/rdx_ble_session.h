#ifndef _RDX_BLE_SESSION_H_
#define _RDX_BLE_SESSION_H_

#include "system/includes.h"

typedef enum {
    RDX_SESSION_UNAUTHORIZED = 0,
    RDX_SESSION_IDENTIFIED,
    RDX_SESSION_AUTHORIZED,
} rdx_session_auth_state_t;

typedef struct {
    u16 con_handle;
    u16 mtu_size;
    u8 connected;
    u8 encrypted;
} rdx_ble_link_state_t;

typedef struct {
    u8 ccc_configured;
    u8 stream_tx_ready;
    rdx_session_auth_state_t auth_state;
} rdx_ble_config_session_t;

void rdx_ble_session_on_connected(u16 con_handle);
void rdx_ble_session_on_disconnected(u16 con_handle);
void rdx_ble_session_reset(void);
void rdx_ble_session_set_mtu(u16 con_handle, u16 mtu_size);
void rdx_ble_session_set_encrypted(u16 con_handle, u8 encrypted);
void rdx_ble_session_set_config_ccc(u16 con_handle, u8 configured);
void rdx_ble_session_set_stream_tx_ready(u16 con_handle, u8 ready);

u8 rdx_ble_session_is_current(u16 con_handle);
const rdx_ble_link_state_t *rdx_ble_session_get_link_state(void);
const rdx_ble_config_session_t *rdx_ble_session_get_config_state(void);

/* These transitions are deliberately not inferred from an ATT write.  The RDX
 * protocol integration must call them only after a verifiable peer handshake. */
u8 rdx_protocol_session_mark_identified(u16 con_handle);
u8 rdx_protocol_session_authorize(u16 con_handle);
void rdx_protocol_session_revoke(u16 con_handle);
u8 rdx_protocol_session_is_authorized(u16 con_handle);

#endif
