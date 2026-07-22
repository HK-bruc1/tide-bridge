#ifndef _RDX_BLE_SESSION_H_
#define _RDX_BLE_SESSION_H_

#include "system/includes.h"

#define RDX_BLE_LINK_MAX               2
#define RDX_BLE_LINK_INVALID_INDEX     0xff

typedef enum {
    RDX_BLE_CAPABILITY_NONE = 0,
    RDX_BLE_CAPABILITY_RDX,
    RDX_BLE_CAPABILITY_HID,
} rdx_ble_capability_t;

typedef enum {
    RDX_BLE_CLAIM_OK = 0,
    RDX_BLE_CLAIM_BUSY,
    RDX_BLE_CLAIM_CONFLICT,
    RDX_BLE_CLAIM_STALE,
} rdx_ble_claim_result_t;

typedef struct {
    void *ble_hdl;
    u16 con_handle;
    u16 mtu_size;
    u32 slot_generation;
    u8 connected;
    u8 encrypted;
    u8 peer_addr_type;
    u8 peer_addr[6];
    u16 conn_interval;
    u16 conn_latency;
    u16 supervision_timeout;
    u8 conn_param_index;
    u8 capability;
    u8 hid_pairing_pending;
    u8 rdx_runtime_active;
    u8 rdx_ccc_configured;
    u8 rdx_stream_tx_ready;
} rdx_ble_link_state_t;

typedef struct {
    u8 active;
    u8 ccc_configured;
    u8 stream_tx_ready;
} rdx_ble_config_session_t;

typedef struct {
    u8 slot_index;
    u32 slot_generation;
    u32 transport_epoch;
} rdx_ble_async_token_t;

void rdx_ble_session_transport_init(void *primary_hdl, void *secondary_hdl);
void rdx_ble_session_transport_deinit(void);
rdx_ble_link_state_t *rdx_ble_session_link_accept(void *hdl, u16 con_handle);
rdx_ble_link_state_t *rdx_ble_session_link_release(void *hdl, u16 con_handle);
rdx_ble_link_state_t *rdx_ble_session_find_by_hdl(void *hdl);
rdx_ble_link_state_t *rdx_ble_session_find(void *hdl, u16 con_handle);
rdx_ble_link_state_t *rdx_ble_session_find_by_con_handle(u16 con_handle);
u8 rdx_ble_session_active_count(void);
u8 rdx_ble_session_link_index(const rdx_ble_link_state_t *link);
rdx_ble_claim_result_t rdx_ble_session_claim_rdx(
    rdx_ble_link_state_t *link,
    u32 expected_slot_generation);
rdx_ble_claim_result_t rdx_ble_session_claim_hid(
    rdx_ble_link_state_t *link,
    u32 expected_slot_generation);
rdx_ble_link_state_t *rdx_ble_session_get_rdx_link(void);
rdx_ble_link_state_t *rdx_ble_session_get_hid_link(void);
u8 rdx_ble_session_link_is_rdx(const rdx_ble_link_state_t *link);
u8 rdx_ble_session_link_is_hid(const rdx_ble_link_state_t *link);
void rdx_ble_session_link_set_hid_pairing_pending(
    rdx_ble_link_state_t *link,
    u8 pending);
u8 rdx_ble_session_link_is_hid_pairing_pending(
    const rdx_ble_link_state_t *link);
rdx_ble_async_token_t rdx_ble_session_token_capture(
    const rdx_ble_link_state_t *link);
rdx_ble_link_state_t *rdx_ble_session_idle_token_resolve(
    const rdx_ble_async_token_t *token);
rdx_ble_link_state_t *rdx_ble_session_link_token_resolve(
    const rdx_ble_async_token_t *token);
void rdx_ble_session_link_set_mtu(rdx_ble_link_state_t *link, u16 mtu_size);
void rdx_ble_session_link_set_encrypted(rdx_ble_link_state_t *link,
                                        u8 encrypted);
void rdx_ble_session_link_set_peer(rdx_ble_link_state_t *link,
                                   u8 peer_addr_type,
                                   const u8 peer_addr[6]);
void rdx_ble_session_link_set_conn_params(rdx_ble_link_state_t *link,
                                          u16 interval,
                                          u16 latency,
                                          u16 supervision_timeout);
void rdx_ble_session_link_set_conn_param_index(rdx_ble_link_state_t *link,
                                                u8 conn_param_index);

void rdx_ble_session_on_connected(u16 con_handle);
void rdx_ble_session_on_disconnected(u16 con_handle);
void rdx_ble_session_reset(void);
void rdx_ble_session_set_mtu(u16 con_handle, u16 mtu_size);
void rdx_ble_session_set_encrypted(u16 con_handle, u8 encrypted);
u8 rdx_ble_session_activate_rdx(u16 con_handle);
u8 rdx_ble_session_is_rdx_active(u16 con_handle);
void rdx_ble_session_set_config_ccc(u16 con_handle, u8 configured);
void rdx_ble_session_set_stream_tx_ready(u16 con_handle, u8 ready);

u8 rdx_ble_session_is_current(u16 con_handle);
const rdx_ble_link_state_t *rdx_ble_session_get_link_state(void);
const rdx_ble_config_session_t *rdx_ble_session_get_config_state(void);

#endif
