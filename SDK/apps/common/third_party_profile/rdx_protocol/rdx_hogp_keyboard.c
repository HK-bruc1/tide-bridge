/*=====================================================================================
 HEADER NAME: rdx_hogp_keyboard.c
 MODULE NAME: RDX BLE HID-over-GATT keyboard submodule.

 PRE-INCLUDE FILES DESCRIPTION:

 GENERAL DESCRIPTION:
    Implements HOGP keyboard state, ATT read/write handlers, SM Just-Works
    handling, HOGP advertising, and Input Report sending. This module is a
    submodule of the RDX GATT Server and reuses the single app_ble wrapper
    handle allocated by RDX.
 =======================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_keyboard.data.bss")
#pragma data_seg(".rdx_hogp_keyboard.data")
#pragma const_seg(".rdx_hogp_keyboard.text.const")
#pragma code_seg(".rdx_hogp_keyboard.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/
/* Include project config first so TCFG_RDX_HOGP_ENABLE is resolved from
 * t2620_project_config.h before rdx_hogp_config.h applies its default. */
#include "sdk_config.h"
#include "app_config.h"

#include "rdx_hogp_keyboard.h"
#include "system/includes.h"
#include "rdx_hogp_profile.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_key_action.h"
#include "ble_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"

#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG                                     "[rdx_hogp]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

/******************************************************************************
* Logging macros
******************************************************************************/
#if RDX_HOGP_LOG_ENABLE
#define RDX_HOGP_LOG(fmt, ...)     y_printf("[HOGP] " fmt "\r", ##__VA_ARGS__)
#define RDX_HOGP_ERROR(fmt, ...)   y_printf("[HOGP_ERR] " fmt "\r", ##__VA_ARGS__)
#if RDX_HOGP_VERBOSE_LOG
#define RDX_HOGP_VERBOSE(fmt, ...) y_printf("[HOGP] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_HOGP_VERBOSE(fmt, ...) /* no verbose log */
#endif
#else
#define RDX_HOGP_LOG(fmt, ...)     /* no log */
#define RDX_HOGP_ERROR(fmt, ...)   /* no log */
#define RDX_HOGP_VERBOSE(fmt, ...) /* no log */
#endif

/******************************************************************************
* Local variables Section
******************************************************************************/
static void *s_hogp_app_ble_hdl = NULL;
static volatile u8 s_hogp_connected = 0;
static volatile u8 s_hid_notify_enabled = 0;
static volatile u8 s_hogp_encrypted = 0;
static u16 s_hid_con_handle = 0;
static rdx_hogp_keyboard_report_t s_hid_input_report = {0};
static u8 s_hid_output_report = RDX_HOGP_OUTPUT_REPORT_DEFAULT_VALUE;

#define RDX_HOGP_PROTOCOL_MODE_REPORT    1

#define RDX_HOGP_CONTROL_POINT_SUSPEND       0
#define RDX_HOGP_CONTROL_POINT_EXIT_SUSPEND  1

/* Local ATT error codes (Bluetooth spec values) -- SDK does not export ATT_ERROR_* macros. */
#define RDX_HOGP_ATT_ERR_INVALID_OFFSET                 0x07
#define RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN    0x0d
#define RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED              0x13

static volatile u8 s_hogp_suspended = 0;
static u8 s_hid_protocol_mode = RDX_HOGP_PROTOCOL_MODE_REPORT;

static void rdx_hogp_current_report_clear(void)
{
    memset((void *)&s_hid_input_report, 0, sizeof(s_hid_input_report));
}

static void rdx_hogp_current_report_set(const u8 *payload, u8 len)
{
    if (payload == NULL || len != RDX_HOGP_KEYBOARD_REPORT_LEN) {
        return;
    }
    memcpy((void *)&s_hid_input_report, payload, sizeof(s_hid_input_report));
}

/* Transition from online HID capability back to offline product behavior.
 * Release while ready is still true so the host can observe key-up, then
 * converge local state even when the transport send fails. */
static void rdx_hogp_ready_drop_cleanup(void)
{
    if (rdx_hogp_keyboard_is_ready()) {
        rdx_hogp_key_action_reset();
    }
    rdx_hogp_current_report_clear();
}

static void hogp_runtime_state_reset(void)
{
    rdx_hogp_current_report_clear();
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hogp_suspended = 0;
    s_hid_protocol_mode = RDX_HOGP_PROTOCOL_MODE_REPORT;
    s_hid_output_report = RDX_HOGP_OUTPUT_REPORT_DEFAULT_VALUE;
}

/******************************************************************************
* Static helper functions
******************************************************************************/
static uint16_t hid_read_helper(const u8 *data, u16 data_len,
                                u16 offset, u8 *buffer,
                                u16 buffer_size)
{
    if (offset >= data_len) {
        return 0;
    }
    uint16_t len = data_len - offset;
    if (buffer) {
        if (len > buffer_size) {
            len = buffer_size;
        }
        memcpy(buffer, data + offset, len);
    }
    return len;
}

static void hogp_runtime_cleanup(void)
{
    rdx_hogp_ready_drop_cleanup();
    hogp_runtime_state_reset();
}

void rdx_hogp_runtime_cleanup(void)
{
    hogp_runtime_cleanup();
}

static void hogp_module_cleanup(void)
{
    hogp_runtime_cleanup();
    s_hogp_app_ble_hdl = NULL;
}

/******************************************************************************
* Lifecycle
******************************************************************************/
void rdx_hogp_init(void *app_ble_hdl)
{
    s_hogp_app_ble_hdl = app_ble_hdl;
    hogp_runtime_state_reset();
    rdx_hogp_dump_state();
}

void rdx_hogp_deinit(void)
{
    hogp_module_cleanup();
}

/******************************************************************************
* ATT routing
******************************************************************************/
u8 rdx_hogp_is_handle(u16 att_handle)
{
    return (att_handle >= HID_SERVICE_START_HANDLE && att_handle <= HID_SERVICE_END_HANDLE) ? 1 : 0;
}

u16 rdx_hogp_att_read(hci_con_handle_t connection_handle,
                      u16 att_handle,
                      u16 offset,
                      u8 *buffer,
                      u16 buffer_size)
{
    if (s_hogp_app_ble_hdl == NULL) {
        return 0;
    }

    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        return hid_read_helper(&s_hid_protocol_mode, 1, offset, buffer, buffer_size);
    case HID_REPORT_MAP_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_report_map, RDX_HOGP_REPORT_MAP_LEN, offset, buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_hid_information, RDX_HOGP_HID_INFORMATION_LEN, offset, buffer, buffer_size);
    case HID_INPUT_REPORT_VALUE_HANDLE:
        return hid_read_helper((const u8 *)&s_hid_input_report,
                               RDX_HOGP_KEYBOARD_REPORT_LEN,
                               offset, buffer, buffer_size);
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        return hid_read_helper(&s_hid_output_report, 1, offset, buffer, buffer_size);
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (buffer && buffer_size >= 2) {
            buffer[0] = multi_att_get_ccc_config(connection_handle, att_handle) & 0xFF;
            buffer[1] = 0;
            RDX_HOGP_VERBOSE("CCC read hdl=0x%04x cfg=0x%02x%02x", att_handle, buffer[0], buffer[1]);
        }
        return 2;
    default:
        return 0;
    }
}

int rdx_hogp_att_write(hci_con_handle_t connection_handle,
                        u16 att_handle,
                        u16 transaction_mode,
                        u16 offset,
                        u8 *buffer,
                        u16 buffer_size)
{
    if (s_hogp_app_ble_hdl == NULL) {
        return 0;
    }

    (void)transaction_mode;
    (void)offset;

    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        if (offset != 0) {
            return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 1) {
            return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        /* This service exposes Report characteristics only; Boot Report
         * characteristics are intentionally absent. */
        if (buffer[0] != RDX_HOGP_PROTOCOL_MODE_REPORT) {
            RDX_HOGP_ERROR("protocol mode rejected val=0x%02x", buffer[0]);
            return RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        s_hid_protocol_mode = buffer[0];
        RDX_HOGP_LOG("protocol mode=%d", s_hid_protocol_mode);
        return 0;
    case HID_CONTROL_POINT_VALUE_HANDLE:
        if (offset != 0) {
            return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 1) {
            return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        if (buffer[0] == RDX_HOGP_CONTROL_POINT_SUSPEND) {
            rdx_hogp_ready_drop_cleanup();
            s_hogp_suspended = 1;
            RDX_HOGP_LOG("control point: suspend");
            rdx_hogp_dump_state();
            return 0;
        }
        if (buffer[0] == RDX_HOGP_CONTROL_POINT_EXIT_SUSPEND) {
            s_hogp_suspended = 0;
            RDX_HOGP_LOG("control point: exit suspend");
            rdx_hogp_dump_state();
            return 0;
        }
        RDX_HOGP_ERROR("control point rejected val=0x%02x", buffer[0]);
        return RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED;
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (offset != 0) {
            return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 2) {
            return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        {
            u16 cfg = buffer[0] | (buffer[1] << 8);
            if (cfg != 0x0000 && cfg != 0x0001) {
                RDX_HOGP_ERROR("CCC rejected cfg=0x%04x", cfg);
                return RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            if (s_hid_notify_enabled && cfg == 0x0000) {
                rdx_hogp_ready_drop_cleanup();
            }
            s_hid_notify_enabled = (cfg == 0x0001) ? 1 : 0;
            multi_att_set_ccc_config(connection_handle, att_handle, cfg);
#if RDX_HOGP_ENCRYPTION_REQUIRED
            if (s_hid_notify_enabled && !s_hogp_encrypted) {
                sm_api_request_pairing(connection_handle);
            }
#endif
            RDX_HOGP_LOG("CCC write hdl=0x%04x cfg=0x%04x notify=%d", att_handle, cfg, s_hid_notify_enabled);
            rdx_hogp_dump_state();
        }
        return 0;
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        if (offset != 0) {
            return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
        }
        if (buffer_size != 1) {
            return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
        }
        s_hid_output_report = buffer[0];
        RDX_HOGP_LOG("output report LED=0x%02x", s_hid_output_report);
        return 0;
    default:
        RDX_HOGP_VERBOSE("write default hdl=0x%04x len=%d data[0]=0x%02x",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    }
}

/******************************************************************************
* Keyboard Report API
******************************************************************************/
u8 rdx_hogp_keyboard_is_connected(void)
{
    return s_hogp_connected ? 1 : 0;
}

u8 rdx_hogp_keyboard_is_ready(void)
{
    if (!s_hogp_connected) {
        return 0;
    }
    if (s_hogp_app_ble_hdl == NULL) {
        return 0;
    }
    if (!s_hid_notify_enabled) {
        return 0;
    }
    if (s_hogp_suspended) {
        return 0;
    }
#if RDX_HOGP_ENCRYPTION_REQUIRED
    if (!s_hogp_encrypted) {
        return 0;
    }
#endif
    return 1;
}

int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report)
{
    u8 payload[RDX_HOGP_KEYBOARD_REPORT_LEN];

    if (report == NULL) {
        return -1;
    }

    if (!rdx_hogp_keyboard_is_ready()) {
        RDX_HOGP_ERROR("report_send skipped: not ready");
        rdx_hogp_dump_state();
        return -1;
    }

    memcpy(payload, report, sizeof(payload));

    RDX_HOGP_LOG("report_send %02x %02x %02x %02x %02x %02x %02x %02x",
                 payload[0], payload[1], payload[2], payload[3],
                 payload[4], payload[5], payload[6], payload[7]);

    int ret = app_ble_att_send_data(s_hogp_app_ble_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    payload, sizeof(payload),
                                    ATT_OP_NOTIFY);
    if (ret == APP_BLE_NO_ERROR) {
        rdx_hogp_current_report_set(payload, sizeof(payload));
    } else {
        RDX_HOGP_ERROR("report_send failed ret=%d", ret);
        rdx_hogp_dump_state();
    }

    return ret;
}

int rdx_hogp_keyboard_release_all(void)
{
    rdx_hogp_keyboard_report_t report = {0};
    return rdx_hogp_keyboard_report_send(&report);
}

/******************************************************************************
* Connection / security events
******************************************************************************/
void rdx_hogp_dump_state(void)
{
    RDX_HOGP_LOG("state conn=%d con=0x%04x ccc=%d enc=%d suspend=%d ready=%d hdl=%p",
                 s_hogp_connected,
                 s_hid_con_handle,
                 s_hid_notify_enabled,
                 s_hogp_encrypted,
                 s_hogp_suspended,
                 rdx_hogp_keyboard_is_ready(),
                 s_hogp_app_ble_hdl);
}

void rdx_hogp_on_connected(u16 con_handle, u8 encrypted)
{
    u16 ccc_config;

    if (s_hogp_connected && s_hid_con_handle == con_handle) {
        return;
    }
    hogp_runtime_state_reset();
    s_hogp_connected = 1;
    s_hid_con_handle = con_handle;
    ccc_config = multi_att_get_ccc_config(
        con_handle, HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE);
    s_hid_notify_enabled = (ccc_config & 0x0001) ? 1 : 0;
    s_hogp_encrypted = encrypted ? 1 : 0;
    s_hogp_suspended = 0;
    s_hid_protocol_mode = RDX_HOGP_PROTOCOL_MODE_REPORT;
    RDX_HOGP_LOG("conn complete hdl=0x%04x restored_ccc=0x%04x",
                 con_handle, ccc_config);
    rdx_hogp_dump_state();
}

void rdx_hogp_on_disconnected(u16 con_handle)
{
    if (!s_hogp_connected || con_handle != s_hid_con_handle) {
        return;
    }
    rdx_hogp_ready_drop_cleanup();
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hogp_suspended = 0;
    RDX_HOGP_LOG("disconnect");
    rdx_hogp_dump_state();
}

void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status)
{
    RDX_HOGP_LOG("encryption_change hdl=0x%04x enabled=%d status=%d",
                 con_handle, enabled, status);

    if (!s_hogp_connected || con_handle != s_hid_con_handle) {
        RDX_HOGP_ERROR("encryption_change ignored: stale handle");
        rdx_hogp_dump_state();
        return;
    }

    u8 encrypted = (enabled && status == 0) ? 1 : 0;
    if (s_hogp_encrypted && !encrypted) {
        rdx_hogp_ready_drop_cleanup();
    }
    s_hogp_encrypted = encrypted;
    if (!s_hogp_encrypted) {
        RDX_HOGP_ERROR("link encryption disabled or failed");
    }

    rdx_hogp_dump_state();
}

void rdx_hogp_on_sm_event(u8 packet_type, u8 *packet, u16 size)
{
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        if (s_hogp_connected &&
            sm_event_just_works_request_get_handle(packet) == s_hid_con_handle) {
            RDX_HOGP_LOG("Just Works pairing request, confirm");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        }
        break;
    default:
        break;
    }
}

#else  /* !(TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)) -- stubs */

void rdx_hogp_init(void *app_ble_hdl) { (void)app_ble_hdl; }
void rdx_hogp_deinit(void) {}
void rdx_hogp_runtime_cleanup(void) {}
u8   rdx_hogp_is_handle(u16 att_handle) { (void)att_handle; return 0; }
u16  rdx_hogp_att_read(hci_con_handle_t ch, u16 h, u16 o, u8 *b, u16 bs) {
    (void)ch; (void)h; (void)o; (void)b; (void)bs; return 0;
}
int  rdx_hogp_att_write(hci_con_handle_t ch, u16 h, u16 tm, u16 o, u8 *b, u16 bs) {
    (void)ch; (void)h; (void)tm; (void)o; (void)b; (void)bs; return 0;
}
void rdx_hogp_on_connected(u16 con_handle, u8 encrypted) {
    (void)con_handle;
    (void)encrypted;
}
void rdx_hogp_on_disconnected(u16 con_handle) { (void)con_handle; }
void rdx_hogp_on_encryption_change(u16 ch, u8 en, u8 st) { (void)ch; (void)en; (void)st; }
void rdx_hogp_on_sm_event(u8 pt, u8 *pk, u16 sz) { (void)pt; (void)pk; (void)sz; }
void rdx_hogp_dump_state(void) {}

int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report)
{
    (void)report;
    return -1;
}

int rdx_hogp_keyboard_release_all(void)
{
    return -1;
}

u8 rdx_hogp_keyboard_is_connected(void)
{
    return 0;
}

u8 rdx_hogp_keyboard_is_ready(void)
{
    return 0;
}

#endif /* TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */
