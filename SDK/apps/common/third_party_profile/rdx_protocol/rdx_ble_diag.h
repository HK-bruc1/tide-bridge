#ifndef RDX_BLE_DIAG_H
#define RDX_BLE_DIAG_H

#include "typedef.h"

/* ABI shared with tools/ble_diag/patch_btctrler.ps1. Append, never renumber. */
enum rdx_ble_diag_event {
    RDX_BLE_DIAG_CONNECT = 1,
    RDX_BLE_DIAG_MIC_CHECK,
    RDX_BLE_DIAG_RX_ERROR,
    RDX_BLE_DIAG_MIC_DISPATCH,
    RDX_BLE_DIAG_ENC_ACL,
    RDX_BLE_DIAG_ENC_CTRL,
    RDX_BLE_DIAG_REMOTE_TERM,
    RDX_BLE_DIAG_LOCAL_TERM,
    RDX_BLE_DIAG_TERM_TX,
    RDX_BLE_DIAG_COMMIT,
    RDX_BLE_DIAG_DISCONNECTED,
    RDX_BLE_DIAG_HOST_REQUEST,
    RDX_BLE_DIAG_APP_REQUEST,
};

enum rdx_ble_diag_app_site {
    RDX_BLE_DIAG_APP_RDX = 1,
    RDX_BLE_DIAG_APP_USB,
    RDX_BLE_DIAG_APP_EXIT,
};

/* Revision 2 scalar ABI. Timer operations are observations, never commands. */
enum rdx_ble_diag_sup_site {
    RDX_BLE_SUP_ALLOC = 1,
    RDX_BLE_SUP_REGISTER,
    RDX_BLE_SUP_CONTEXT,
    RDX_BLE_SUP_PERIOD,
    RDX_BLE_SUP_REMOVE,
    RDX_BLE_SUP_TIMEOUT_ENTER,
    RDX_BLE_SUP_TIMEOUT_REASON,
    RDX_BLE_SUP_COMMIT,
    RDX_BLE_SUP_FREE,
    RDX_BLE_SUP_UPDATE_TEMP,
    RDX_BLE_SUP_UPDATE_RESTORE,
    RDX_BLE_SUP_SUBRATE,
};

enum rdx_ble_diag_sup_activity_kind {
    RDX_BLE_SUP_RX = 1,
    RDX_BLE_SUP_EVENT,
};

/* flags: proc[7:0], wait_ack!=0[8], role[23:16], enc[31:24].
 * TIMEOUT_REASON value: actual reason[7:0], vendor wait_ack!=0[8].
 * RX sample: event counter[15:0], vendor invalid[23:16].
 * reset_id != 0 means the vendor called reset, not that an OS deadline was read. */
void rdx_ble_diag_sup_state(u32 site, u32 handle, u32 timer_id, u32 value,
                            u32 state, u32 timeout_10ms, u32 event_count, u32 flags);
void rdx_ble_diag_sup_activity(u32 kind, u32 handle, u32 sample, u32 reset_id);
void rdx_ble_diag_sup_modify_result(u32 timer_id, u32 requested_ms, int result);

/* Controller/IRQ callers only copy scalar metadata; no formatting or UART. */
void rdx_ble_diag_record(u32 event, u16 handle, u32 reason, u32 detail);
void rdx_ble_diag_init(void);
void rdx_ble_diag_flush_request(void);
void rdx_ble_diag_app_request(u16 handle, u8 site);

#endif
