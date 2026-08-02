#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hid_service.data.bss")
#pragma data_seg(".rdx_hid_service.data")
#pragma const_seg(".rdx_hid_service.text.const")
#pragma code_seg(".rdx_hid_service.text")
#endif

#include "sdk_config.h"
#include "app_config.h"

#include "rdx_hid_service.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_profile.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_subscription_store.h"
#include "rdx_codex_micro.h"
#include "rdx_gatt_profile.h"
#include "ble_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#if RDX_HOGP_LOG_ENABLE
#define RDX_HID_LOG(fmt, ...)       y_printf("[HID_CORE] " fmt "\r", ##__VA_ARGS__)
#define RDX_HID_ERROR(fmt, ...)     y_printf("[HID_CORE_ERR] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_HID_LOG(fmt, ...)
#define RDX_HID_ERROR(fmt, ...)
#endif

#define RDX_HID_PROTOCOL_MODE_REPORT              1
#define RDX_HID_CONTROL_POINT_SUSPEND              0
#define RDX_HID_CONTROL_POINT_EXIT_SUSPEND         1
#define RDX_HID_ATT_ERR_INVALID_OFFSET                 0x07
#define RDX_HID_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN    0x0d
#define RDX_HID_ATT_ERR_UNLIKELY_ERROR                 0x0e
#define RDX_HID_ATT_ERR_INSUFFICIENT_ENCRYPTION        0x0f
#define RDX_HID_ATT_ERR_VALUE_NOT_ALLOWED              0x13

typedef struct {
    void *app_ble_hdl;
    u16 con_handle;
    volatile u8 connected;
    volatile u8 encrypted;
    volatile u8 suspended;
    u8 protocol_mode;
    volatile u8 keyboard_notify_enabled;
    volatile u8 codex_notify_enabled;
    u8 peer_identity_valid;
    u8 peer_identity[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN];
    u8 subscription_update_pending_bits;
    u8 subscription_update_values;
} rdx_hid_service_state_t;

static rdx_hid_service_state_t s_hid;

static u16 rdx_hid_read_helper(const u8 *data, u16 data_len, u16 offset,
                               u8 *buffer, u16 buffer_size)
{
    u16 len;

    if (offset >= data_len) {
        return 0;
    }
    len = data_len - offset;
    if (buffer) {
        if (len > buffer_size) {
            len = buffer_size;
        }
        memcpy(buffer, data + offset, len);
    }
    return len;
}

static void rdx_hid_runtime_state_reset(void)
{
    s_hid.connected = 0;
    s_hid.con_handle = 0;
    s_hid.keyboard_notify_enabled = 0;
    s_hid.codex_notify_enabled = 0;
    s_hid.encrypted = 0;
    s_hid.suspended = 0;
    s_hid.protocol_mode = RDX_HID_PROTOCOL_MODE_REPORT;
    s_hid.subscription_update_pending_bits = 0;
    s_hid.subscription_update_values = 0;
    rdx_hogp_keyboard_runtime_reset();
}

static void rdx_hid_peer_identity_reset(void)
{
    s_hid.peer_identity_valid = 0;
    memset(s_hid.peer_identity, 0, sizeof(s_hid.peer_identity));
}

static u8 rdx_hid_peer_identity_is_valid(const u8 *peer_addr)
{
    u8 all_zero = 1;
    u8 all_ff = 1;
    u8 i;

    if (!peer_addr) {
        return 0;
    }
    for (i = 0; i < RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN; i++) {
        if (peer_addr[i] != 0x00) {
            all_zero = 0;
        }
        if (peer_addr[i] != 0xff) {
            all_ff = 0;
        }
    }
    return (!all_zero && !all_ff) ? 1 : 0;
}

static void rdx_hid_peer_identity_set(const u8 *peer_addr)
{
    if (!rdx_hid_peer_identity_is_valid(peer_addr)) {
        return;
    }
    s_hid.peer_identity_valid = 1;
    memcpy(s_hid.peer_identity, peer_addr, sizeof(s_hid.peer_identity));
    RDX_HID_LOG("peer identity hdl=0x%04x %02x:%02x:%02x:%02x:%02x:%02x",
                s_hid.con_handle, peer_addr[0], peer_addr[1], peer_addr[2],
                peer_addr[3], peer_addr[4], peer_addr[5]);
}

static u8 rdx_hid_peer_identity_get(u8 *peer_addr)
{
    if (s_hid.peer_identity_valid) {
        memcpy(peer_addr, s_hid.peer_identity, sizeof(s_hid.peer_identity));
        return 1;
    }
    return 0;
}

static void rdx_hid_subscription_update_queue(u8 subscription_bit,
                                              u8 enabled)
{
    s_hid.subscription_update_pending_bits |= subscription_bit;
    if (enabled) {
        s_hid.subscription_update_values |= subscription_bit;
    } else {
        s_hid.subscription_update_values &= ~subscription_bit;
    }
}

static void rdx_hid_subscription_update_cancel(u8 subscription_bit)
{
    s_hid.subscription_update_pending_bits &= ~subscription_bit;
    s_hid.subscription_update_values &= ~subscription_bit;
}

static u8 rdx_hid_subscription_disable_pending(u8 subscription_bit)
{
    return (s_hid.subscription_update_pending_bits & subscription_bit) &&
           !(s_hid.subscription_update_values & subscription_bit);
}

static u8 rdx_hid_report_subscription_bit(rdx_hid_report_id_t report_id)
{
    if (report_id == RDX_HID_REPORT_KEYBOARD) {
        return RDX_HOGP_SUBSCRIPTION_KEYBOARD;
    }
    if (report_id == RDX_HID_REPORT_CODEX) {
        return RDX_HOGP_SUBSCRIPTION_CODEX;
    }
    return 0;
}

static u8 rdx_hid_supported_subscription_bits(void)
{
    u8 supported_bits = 0;

#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
    supported_bits |= RDX_HOGP_SUBSCRIPTION_KEYBOARD;
#endif
#if TCFG_RDX_CODEX_MICRO_MODE
    supported_bits |= RDX_HOGP_SUBSCRIPTION_CODEX;
#endif
    return supported_bits;
}

static int rdx_hid_subscription_update_one(
    const u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN],
    u8 subscription_bit)
{
    u8 enabled =
        (s_hid.subscription_update_values & subscription_bit) ? 1 : 0;

    if (rdx_hogp_subscription_store_update(peer_addr, subscription_bit,
                                           enabled)) {
        RDX_HID_ERROR("subscription persist failed bit=0x%02x enabled=%d",
                      subscription_bit, enabled);
        return -1;
    }
    rdx_hid_subscription_update_cancel(subscription_bit);
    RDX_HID_LOG("subscription persisted bit=0x%02x enabled=%d",
                subscription_bit, enabled);
    return 0;
}

static int rdx_hid_subscription_update_flush(void)
{
    u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN];

    if (!s_hid.subscription_update_pending_bits) {
        return 0;
    }
    if (!s_hid.connected ||
        !rdx_hid_peer_identity_get(peer_addr)) {
        return 1;
    }
    if ((s_hid.subscription_update_pending_bits &
         RDX_HOGP_SUBSCRIPTION_KEYBOARD) &&
        rdx_hid_subscription_update_one(
            peer_addr, RDX_HOGP_SUBSCRIPTION_KEYBOARD)) {
        return -1;
    }
    if ((s_hid.subscription_update_pending_bits &
         RDX_HOGP_SUBSCRIPTION_CODEX) &&
        rdx_hid_subscription_update_one(peer_addr,
                                         RDX_HOGP_SUBSCRIPTION_CODEX)) {
        return -1;
    }
    return 0;
}

static void rdx_hid_subscription_restore_if_available(void)
{
    u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN];
    u8 subscription_bits;
    u8 restored_bits = 0;

    if (!s_hid.connected || !s_hid.encrypted ||
        !rdx_hid_peer_identity_get(peer_addr)) {
        return;
    }
    subscription_bits = rdx_hogp_subscription_store_get(peer_addr);
#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
    if ((subscription_bits & RDX_HOGP_SUBSCRIPTION_KEYBOARD) &&
        !s_hid.keyboard_notify_enabled &&
        !rdx_hid_subscription_disable_pending(
            RDX_HOGP_SUBSCRIPTION_KEYBOARD)) {
        multi_att_set_ccc_config(
            s_hid.con_handle,
            HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE, 0x0001);
        s_hid.keyboard_notify_enabled = 1;
        restored_bits |= RDX_HOGP_SUBSCRIPTION_KEYBOARD;
    }
#endif
#if TCFG_RDX_CODEX_MICRO_MODE
    if ((subscription_bits & RDX_HOGP_SUBSCRIPTION_CODEX) &&
        !s_hid.codex_notify_enabled &&
        !rdx_hid_subscription_disable_pending(RDX_HOGP_SUBSCRIPTION_CODEX)) {
        multi_att_set_ccc_config(
            s_hid.con_handle,
            HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE, 0x0001);
        s_hid.codex_notify_enabled = 1;
        restored_bits |= RDX_HOGP_SUBSCRIPTION_CODEX;
    }
#endif
    if (restored_bits) {
        RDX_HID_LOG("subscription restored hdl=0x%04x bits=0x%02x",
                    s_hid.con_handle, restored_bits);
        rdx_hid_service_dump_state();
    }
}

static void rdx_hid_report_ready_drop(rdx_hid_report_id_t report_id)
{
    if (report_id == RDX_HID_REPORT_KEYBOARD) {
        rdx_hogp_keyboard_ready_drop_cleanup();
    } else if (report_id == RDX_HID_REPORT_CODEX) {
        rdx_codex_micro_ready_drop_cleanup();
    }
}

static void rdx_hid_service_runtime_cleanup(void)
{
    rdx_hid_report_ready_drop(RDX_HID_REPORT_KEYBOARD);
    rdx_hid_report_ready_drop(RDX_HID_REPORT_CODEX);
    rdx_hid_runtime_state_reset();
}

void rdx_hid_service_init(void *app_ble_hdl)
{
    memset(&s_hid, 0, sizeof(s_hid));
    if (rdx_hogp_subscription_store_init()) {
        RDX_HID_ERROR("subscription store migration deferred");
    }
    s_hid.app_ble_hdl = app_ble_hdl;
    rdx_hid_runtime_state_reset();
    rdx_hid_peer_identity_reset();
    rdx_hid_service_dump_state();
}

void rdx_hid_service_deinit(void)
{
    rdx_hid_service_runtime_cleanup();
    rdx_hid_peer_identity_reset();
    s_hid.app_ble_hdl = NULL;
}

u16 rdx_hid_service_att_read(hci_con_handle_t connection_handle,
                             u16 att_handle, u16 offset,
                             u8 *buffer, u16 buffer_size)
{
    if (!s_hid.app_ble_hdl) {
        return 0;
    }
    if (s_hid.encrypted && s_hid.subscription_update_pending_bits) {
        rdx_hid_subscription_update_flush();
    }

    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        return rdx_hid_read_helper(&s_hid.protocol_mode, 1, offset,
                                   buffer, buffer_size);
    case HID_REPORT_MAP_VALUE_HANDLE:
        return rdx_hid_read_helper(rdx_hogp_report_map,
                                   RDX_HOGP_REPORT_MAP_LEN, offset,
                                   buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return rdx_hid_read_helper(rdx_hogp_hid_information,
                                   RDX_HOGP_HID_INFORMATION_LEN, offset,
                                   buffer, buffer_size);
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (buffer && buffer_size >= 2) {
            buffer[0] = multi_att_get_ccc_config(connection_handle,
                                                 att_handle) & 0xff;
            buffer[1] = 0;
        }
        return 2;
#if TCFG_RDX_CODEX_MICRO_MODE
    case HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        {
            u16 cfg = multi_att_get_ccc_config(connection_handle, att_handle);
            u8 value[2] = {(u8)(cfg & 0xff), (u8)(cfg >> 8)};
            return rdx_hid_read_helper(value, sizeof(value), offset,
                                       buffer, buffer_size);
        }
#endif
    case HID_INPUT_REPORT_VALUE_HANDLE:
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        return rdx_hogp_keyboard_att_read(att_handle, offset, buffer,
                                          buffer_size);
#if TCFG_RDX_CODEX_MICRO_MODE
    case HID_CODEX_INPUT_REPORT_VALUE_HANDLE:
    case HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE:
        return rdx_codex_micro_att_read(connection_handle, att_handle,
                                        offset, buffer, buffer_size);
#endif
    default:
        return 0;
    }
}

int rdx_hid_service_att_write(hci_con_handle_t connection_handle,
                              u16 att_handle, u16 transaction_mode,
                              u16 offset, u8 *buffer, u16 buffer_size)
{
    u16 cfg;
    u8 previous_notify;
    int persist_result;

    (void)transaction_mode;
    if (!s_hid.app_ble_hdl) {
        return 0;
    }

#if RDX_HOGP_ENCRYPTION_REQUIRED
    if (!s_hid.encrypted) {
        if (att_handle == HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE
#if TCFG_RDX_CODEX_MICRO_MODE
            || att_handle ==
               HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE
#endif
            ) {
            sm_api_request_pairing(connection_handle);
        }
        RDX_HID_ERROR("write rejected: encryption required hdl=0x%04x",
                      att_handle);
        return RDX_HID_ATT_ERR_INSUFFICIENT_ENCRYPTION;
    }
#endif

    if (s_hid.subscription_update_pending_bits &&
        rdx_hid_subscription_update_flush() < 0) {
        return RDX_HID_ATT_ERR_UNLIKELY_ERROR;
    }

    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        if (offset != 0) {
            return RDX_HID_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 1) {
            return RDX_HID_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        if (buffer[0] != RDX_HID_PROTOCOL_MODE_REPORT) {
            return RDX_HID_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        s_hid.protocol_mode = buffer[0];
        return 0;
    case HID_CONTROL_POINT_VALUE_HANDLE:
        if (offset != 0) {
            return RDX_HID_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 1) {
            return RDX_HID_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        if (buffer[0] == RDX_HID_CONTROL_POINT_SUSPEND) {
            rdx_hid_report_ready_drop(RDX_HID_REPORT_KEYBOARD);
            rdx_hid_report_ready_drop(RDX_HID_REPORT_CODEX);
            s_hid.suspended = 1;
            rdx_hid_service_dump_state();
            return 0;
        }
        if (buffer[0] == RDX_HID_CONTROL_POINT_EXIT_SUSPEND) {
            s_hid.suspended = 0;
            rdx_hid_service_dump_state();
            return 0;
        }
        return RDX_HID_ATT_ERR_VALUE_NOT_ALLOWED;
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (offset != 0) {
            return RDX_HID_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 2) {
            return RDX_HID_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        cfg = buffer[0] | (buffer[1] << 8);
        if (cfg != 0x0000 && cfg != 0x0001) {
            return RDX_HID_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        previous_notify = s_hid.keyboard_notify_enabled;
        if (previous_notify && cfg == 0x0000) {
            rdx_hid_report_ready_drop(RDX_HID_REPORT_KEYBOARD);
        }
        s_hid.keyboard_notify_enabled = cfg == 0x0001 ? 1 : 0;
        multi_att_set_ccc_config(connection_handle, att_handle, cfg);
        rdx_hid_subscription_update_queue(
            RDX_HOGP_SUBSCRIPTION_KEYBOARD,
            s_hid.keyboard_notify_enabled);
        persist_result = rdx_hid_subscription_update_flush();
        if (persist_result < 0) {
            rdx_hid_subscription_update_cancel(
                RDX_HOGP_SUBSCRIPTION_KEYBOARD);
            s_hid.keyboard_notify_enabled = previous_notify;
            multi_att_set_ccc_config(connection_handle, att_handle,
                                     previous_notify ? 0x0001 : 0x0000);
            return RDX_HID_ATT_ERR_UNLIKELY_ERROR;
        }
        rdx_hid_service_dump_state();
        return 0;
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        return rdx_hogp_keyboard_att_write(offset, buffer, buffer_size);
#if TCFG_RDX_CODEX_MICRO_MODE
    case HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (offset != 0) {
            return RDX_HID_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 2) {
            return RDX_HID_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        cfg = buffer[0] | (buffer[1] << 8);
        if (cfg != 0x0000 && cfg != 0x0001) {
            return RDX_HID_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        previous_notify = s_hid.codex_notify_enabled;
        if (previous_notify && cfg == 0x0000) {
            rdx_hid_report_ready_drop(RDX_HID_REPORT_CODEX);
        }
        s_hid.codex_notify_enabled = cfg == 0x0001 ? 1 : 0;
        multi_att_set_ccc_config(connection_handle, att_handle, cfg);
        rdx_hid_subscription_update_queue(
            RDX_HOGP_SUBSCRIPTION_CODEX, s_hid.codex_notify_enabled);
        persist_result = rdx_hid_subscription_update_flush();
        if (persist_result < 0) {
            rdx_hid_subscription_update_cancel(RDX_HOGP_SUBSCRIPTION_CODEX);
            s_hid.codex_notify_enabled = previous_notify;
            multi_att_set_ccc_config(connection_handle, att_handle,
                                     previous_notify ? 0x0001 : 0x0000);
            return RDX_HID_ATT_ERR_UNLIKELY_ERROR;
        }
        rdx_hid_service_dump_state();
        return 0;
    case HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE:
        return rdx_codex_micro_output_write(connection_handle, offset,
                                            buffer, buffer_size);
#endif
    default:
        return 0;
    }
}

u8 rdx_hid_service_is_connected(void)
{
    return s_hid.connected ? 1 : 0;
}

u8 rdx_hid_service_route_is_active(void)
{
    return (s_hid.connected && s_hid.app_ble_hdl) ? 1 : 0;
}

u8 rdx_hid_report_is_ready(rdx_hid_report_id_t report_id)
{
    u8 notify_enabled;

    if (!s_hid.connected || !s_hid.app_ble_hdl || s_hid.suspended) {
        return 0;
    }
    if (report_id == RDX_HID_REPORT_KEYBOARD) {
        notify_enabled = s_hid.keyboard_notify_enabled;
    } else if (report_id == RDX_HID_REPORT_CODEX) {
#if TCFG_RDX_CODEX_MICRO_MODE
        notify_enabled = s_hid.codex_notify_enabled;
#else
        notify_enabled = 0;
#endif
    } else {
        return 0;
    }
    if (!notify_enabled) {
        return 0;
    }
#if RDX_HOGP_ENCRYPTION_REQUIRED
    if (!s_hid.encrypted) {
        return 0;
    }
#endif
    return 1;
}

int rdx_hid_report_notify(rdx_hid_report_id_t report_id,
                          u16 value_handle, u8 *data, u16 len)
{
    if (!data || !len || !rdx_hid_report_is_ready(report_id)) {
        return -1;
    }
    if ((report_id == RDX_HID_REPORT_KEYBOARD &&
         value_handle != HID_INPUT_REPORT_VALUE_HANDLE)
#if TCFG_RDX_CODEX_MICRO_MODE
        || (report_id == RDX_HID_REPORT_CODEX &&
            value_handle != HID_CODEX_INPUT_REPORT_VALUE_HANDLE)
#endif
        ) {
        return -1;
    }
    return app_ble_att_send_data(s_hid.app_ble_hdl, value_handle,
                                 data, len, ATT_OP_NOTIFY);
}

u8 rdx_hid_service_peer_has_persisted_subscription(const u8 *peer_identity)
{
    u8 subscription_bits;

    if (!rdx_hid_peer_identity_is_valid(peer_identity)) {
        return 0;
    }
    subscription_bits = rdx_hogp_subscription_store_get(peer_identity);
    return (subscription_bits & rdx_hid_supported_subscription_bits()) ?
           1 : 0;
}

int rdx_hid_service_peer_subscription_update(
    const u8 *peer_identity, rdx_hid_report_id_t report_id, u8 enabled)
{
    u8 subscription_bit = rdx_hid_report_subscription_bit(report_id);

    if (!subscription_bit ||
        !rdx_hid_peer_identity_is_valid(peer_identity)) {
        return -1;
    }
    return rdx_hogp_subscription_store_update(
        peer_identity, subscription_bit, enabled ? 1 : 0);
}

void rdx_hid_service_dump_state(void)
{
    RDX_HID_LOG("state conn=%d con=0x%04x kbd_ccc=%d codex_ccc=%d enc=%d suspend=%d kbd_ready=%d codex_ready=%d hdl=%p",
                s_hid.connected, s_hid.con_handle,
                s_hid.keyboard_notify_enabled, s_hid.codex_notify_enabled,
                s_hid.encrypted, s_hid.suspended,
                rdx_hid_report_is_ready(RDX_HID_REPORT_KEYBOARD),
                rdx_hid_report_is_ready(RDX_HID_REPORT_CODEX),
                s_hid.app_ble_hdl);
}

void rdx_hid_service_on_connected_with_hdl(void *app_ble_hdl,
                                           u16 con_handle, u8 encrypted,
                                           const u8 *peer_identity)
{
    u16 keyboard_ccc_config = 0;
    u16 codex_ccc_config = 0;

    if (!app_ble_hdl ||
        !rdx_hid_peer_identity_is_valid(peer_identity)) {
        RDX_HID_ERROR("owner attach rejected: peer identity unavailable");
        return;
    }
    if (s_hid.connected && s_hid.con_handle == con_handle) {
        return;
    }
    rdx_hid_runtime_state_reset();
    rdx_hid_peer_identity_reset();
    s_hid.app_ble_hdl = app_ble_hdl;
    s_hid.connected = 1;
    s_hid.con_handle = con_handle;
    rdx_hid_peer_identity_set(peer_identity);
#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
    keyboard_ccc_config = multi_att_get_ccc_config(
        con_handle, HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE);
    s_hid.keyboard_notify_enabled =
        (keyboard_ccc_config & 0x0001) ? 1 : 0;
#else
    s_hid.keyboard_notify_enabled = 0;
#endif
#if TCFG_RDX_CODEX_MICRO_MODE
    codex_ccc_config = multi_att_get_ccc_config(
        con_handle, HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE);
    s_hid.codex_notify_enabled = (codex_ccc_config & 0x0001) ? 1 : 0;
#endif
    s_hid.encrypted = encrypted ? 1 : 0;
    s_hid.suspended = 0;
    s_hid.protocol_mode = RDX_HID_PROTOCOL_MODE_REPORT;
#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
    if (s_hid.keyboard_notify_enabled) {
        rdx_hid_subscription_update_queue(
            RDX_HOGP_SUBSCRIPTION_KEYBOARD, 1);
    }
#endif
#if TCFG_RDX_CODEX_MICRO_MODE
    if (s_hid.codex_notify_enabled) {
        rdx_hid_subscription_update_queue(RDX_HOGP_SUBSCRIPTION_CODEX, 1);
    }
#endif
    rdx_hid_subscription_update_flush();
    rdx_hid_subscription_restore_if_available();
#if TCFG_RDX_CODEX_MICRO_MODE != RDX_CODEX_MICRO_MODE_VENDOR_ONLY
    keyboard_ccc_config = multi_att_get_ccc_config(
        con_handle, HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE);
#endif
#if TCFG_RDX_CODEX_MICRO_MODE
    codex_ccc_config = multi_att_get_ccc_config(
        con_handle, HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE);
#endif
    RDX_HID_LOG("conn complete hdl=0x%04x restored_ccc=0x%04x codex_ccc=0x%04x",
                con_handle, keyboard_ccc_config, codex_ccc_config);
    rdx_hid_service_dump_state();
}

void rdx_hid_service_on_disconnected(u16 con_handle)
{
    if (!s_hid.connected || con_handle != s_hid.con_handle) {
        return;
    }
    if (s_hid.subscription_update_pending_bits) {
        rdx_hid_subscription_update_flush();
    }
    rdx_hid_report_ready_drop(RDX_HID_REPORT_KEYBOARD);
    rdx_hid_report_ready_drop(RDX_HID_REPORT_CODEX);
    s_hid.connected = 0;
    s_hid.con_handle = 0;
    s_hid.keyboard_notify_enabled = 0;
    s_hid.codex_notify_enabled = 0;
    rdx_codex_micro_runtime_reset();
    s_hid.encrypted = 0;
    s_hid.suspended = 0;
    rdx_hid_peer_identity_reset();
    RDX_HID_LOG("disconnect");
    rdx_hid_service_dump_state();
}

void rdx_hid_service_on_encryption_change(u16 con_handle, u8 enabled,
                                          u8 status)
{
    u8 encrypted;

    if (!s_hid.connected || con_handle != s_hid.con_handle) {
        RDX_HID_ERROR("encryption_change ignored: stale handle");
        return;
    }
    encrypted = (enabled && status == 0) ? 1 : 0;
    if (s_hid.encrypted && !encrypted) {
        rdx_hid_report_ready_drop(RDX_HID_REPORT_KEYBOARD);
        rdx_hid_report_ready_drop(RDX_HID_REPORT_CODEX);
    }
    s_hid.encrypted = encrypted;
    if (s_hid.encrypted) {
        rdx_hid_subscription_update_flush();
        rdx_hid_subscription_restore_if_available();
    } else {
        RDX_HID_ERROR("link encryption disabled or failed");
    }
    rdx_hid_service_dump_state();
}

void rdx_hid_service_on_sm_event(u8 packet_type, u8 *packet, u16 size)
{
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    if (hci_event_packet_get_type(packet) == SM_EVENT_JUST_WORKS_REQUEST &&
        s_hid.connected &&
        sm_event_just_works_request_get_handle(packet) == s_hid.con_handle) {
        sm_just_works_confirm(
            sm_event_just_works_request_get_handle(packet));
    }
}

#else

void rdx_hid_service_init(void *h) { (void)h; }
void rdx_hid_service_deinit(void) {}
u16 rdx_hid_service_att_read(hci_con_handle_t c, u16 h, u16 o, u8 *b, u16 s)
{ (void)c; (void)h; (void)o; (void)b; (void)s; return 0; }
int rdx_hid_service_att_write(hci_con_handle_t c, u16 h, u16 t, u16 o,
                              u8 *b, u16 s)
{ (void)c; (void)h; (void)t; (void)o; (void)b; (void)s; return 0; }
void rdx_hid_service_on_connected_with_hdl(void *h, u16 c, u8 e,
                                           const u8 *p)
{ (void)h; (void)c; (void)e; (void)p; }
void rdx_hid_service_on_disconnected(u16 c) { (void)c; }
void rdx_hid_service_on_encryption_change(u16 c, u8 e, u8 s)
{ (void)c; (void)e; (void)s; }
void rdx_hid_service_on_sm_event(u8 t, u8 *p, u16 s)
{ (void)t; (void)p; (void)s; }
u8 rdx_hid_service_peer_has_persisted_subscription(const u8 *p)
{ (void)p; return 0; }
int rdx_hid_service_peer_subscription_update(const u8 *p,
                                              rdx_hid_report_id_t r, u8 e)
{ (void)p; (void)r; (void)e; return -1; }
u8 rdx_hid_service_is_connected(void) { return 0; }
u8 rdx_hid_service_route_is_active(void) { return 0; }
u8 rdx_hid_report_is_ready(rdx_hid_report_id_t r) { (void)r; return 0; }
int rdx_hid_report_notify(rdx_hid_report_id_t r, u16 h,
                          u8 *d, u16 l)
{ (void)r; (void)h; (void)d; (void)l; return -1; }
void rdx_hid_service_dump_state(void) {}

#endif
