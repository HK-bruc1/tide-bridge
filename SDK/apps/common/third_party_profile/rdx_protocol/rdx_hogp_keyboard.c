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
#include "ble_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"

#include "rdx_ble_server.h"
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
static volatile u8 s_hogp_mode = 0;
static volatile u8 s_hogp_connected = 0;
static volatile u8 s_hid_notify_enabled = 0;
static volatile u8 s_hogp_encrypted = 0;
static u16 s_hid_con_handle = 0;
static u8 s_hid_input_report[8] = {0};
static u16 s_hogp_key_up_timer = 0;
static u32 s_hogp_generation = 0;
static u32 s_hogp_key_generation = 0;

/* Default 5-key keymap (A, B, C, D, E) — centralized in rdx_hogp_config.h */
static const u8 key_to_hid_usage[5] = {
    RDX_HOGP_KEYMAP_A,
    RDX_HOGP_KEYMAP_B,
    RDX_HOGP_KEYMAP_C,
    RDX_HOGP_KEYMAP_D,
    RDX_HOGP_KEYMAP_E,
};

static u8 hid_protocol_mode = 1;  // Report Protocol

/******************************************************************************
* Forward references to RDX BLE Server helpers
******************************************************************************/
extern char *rdx_ble_server_get_local_name(void);
extern int rdx_ble_server_adv_enable(u8 enable);
extern void rdx_ble_server_app_disconnect(void);

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

static void hogp_cancel_key_up_timer(void)
{
    if (s_hogp_key_up_timer) {
        sys_timeout_del(s_hogp_key_up_timer);
        s_hogp_key_up_timer = 0;
    }
}

static void hogp_key_up_timeout(void *priv)
{
    u8 usage = (u8)(u32)priv;

    s_hogp_key_up_timer = 0;

    if (s_hogp_app_ble_hdl == NULL) {
        RDX_HOGP_ERROR("key_up timeout ignored: server hdl NULL");
        return;
    }
    if (s_hogp_generation != s_hogp_key_generation) {
        RDX_HOGP_ERROR("key_up timeout ignored: stale generation");
        return;
    }

    rdx_hogp_key_send_usage(usage, 0);
}

static void hogp_adv_start_internal(void)
{
    u8 advData[ADV_RSP_PACKET_MAX];
    u8 len;

    if (s_hogp_app_ble_hdl == NULL) {
        return;
    }

    app_ble_adv_enable(s_hogp_app_ble_hdl, 0);
    app_ble_rsp_data_set(s_hogp_app_ble_hdl, NULL, 0);

    len = rdx_hogp_fill_adv_data(advData, sizeof(advData));
    app_ble_set_adv_param(s_hogp_app_ble_hdl,
                          rdx_ble_server_get_info()->adv_interval_min,
                          APP_ADV_IND, APP_ADV_CHANNEL_ALL);
    app_ble_adv_data_set(s_hogp_app_ble_hdl, advData, len);
    app_ble_adv_enable(s_hogp_app_ble_hdl, 1);

    RDX_HOGP_LOG("HID advertising started");
}

static void hogp_adv_stop_internal(void)
{
    if (s_hogp_app_ble_hdl == NULL) {
        return;
    }

    app_ble_adv_enable(s_hogp_app_ble_hdl, 0);
    rdx_ble_server_adv_enable(1);

    RDX_HOGP_LOG("HID advertising stopped, restore RDX advertising");
}

static void hogp_runtime_cleanup(void)
{
    hogp_cancel_key_up_timer();
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
    s_hogp_mode = 0;
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hogp_generation++;
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
    hogp_cancel_key_up_timer();
    s_hogp_generation++;
    s_hogp_app_ble_hdl = app_ble_hdl;
    s_hogp_mode = 0;
    s_hogp_connected = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hid_con_handle = 0;
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
    rdx_hogp_dump_state();
}

void rdx_hogp_deinit(void)
{
    hogp_module_cleanup();
}

/******************************************************************************
* Mode
******************************************************************************/
u8 rdx_hogp_mode_get(void)
{
    return s_hogp_mode;
}

void rdx_hogp_mode_set(u8 enable)
{
    u8 new_mode = enable ? 1 : 0;

    if (new_mode == s_hogp_mode) {
        return;
    }

    if (rdx_ble_server_get_info()->ble_conn) {
        RDX_HOGP_LOG("active ble conn, disconnect before mode switch");
        rdx_ble_server_app_disconnect();
    }

    if (new_mode) {
        s_hogp_mode = 1;
        hogp_adv_start_internal();
        rdx_hogp_dump_state();
    } else {
        hogp_adv_stop_internal();
        hogp_runtime_cleanup();
        rdx_hogp_dump_state();
    }
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
        return hid_read_helper(&hid_protocol_mode, 1, offset, buffer, buffer_size);
    case HID_REPORT_MAP_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_report_map, RDX_HOGP_REPORT_MAP_LEN, offset, buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return hid_read_helper(rdx_hogp_hid_information, RDX_HOGP_HID_INFORMATION_LEN, offset, buffer, buffer_size);
    case HID_INPUT_REPORT_VALUE_HANDLE:
        return hid_read_helper(s_hid_input_report, sizeof(s_hid_input_report), offset, buffer, buffer_size);
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
    case HID_CONTROL_POINT_VALUE_HANDLE:
        if (buffer_size >= 1) {
            RDX_HOGP_VERBOSE("ctrl point hdl=0x%04x val=0x%02x", att_handle, buffer[0]);
        }
        return 0;
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (buffer_size >= 2) {
            u16 cfg = buffer[0] | (buffer[1] << 8);
            s_hid_notify_enabled = (cfg & 0x01);
            multi_att_set_ccc_config(connection_handle, att_handle, cfg);
            RDX_HOGP_LOG("CCC write hdl=0x%04x cfg=0x%04x notify=%d", att_handle, cfg, s_hid_notify_enabled);
            rdx_hogp_dump_state();
        }
        return 0;
    case HID_INPUT_REPORT_VALUE_HANDLE:
        RDX_HOGP_VERBOSE("input report write hdl=0x%04x len=%d data[0]=0x%02x",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    default:
        RDX_HOGP_VERBOSE("write default hdl=0x%04x len=%d data[0]=0x%02x",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    }
}

/******************************************************************************
* Connection / security events
******************************************************************************/
void rdx_hogp_dump_state(void)
{
    RDX_HOGP_LOG("state mode=%d conn=%d con=0x%04x ccc=%d enc=%d hdl=%p",
                 s_hogp_mode,
                 s_hogp_connected,
                 s_hid_con_handle,
                 s_hid_notify_enabled,
                 s_hogp_encrypted,
                 s_hogp_app_ble_hdl);
}

void rdx_hogp_on_connected(u16 con_handle)
{
    if (!s_hogp_mode) {
        return;
    }
    s_hogp_connected = 1;
    s_hid_con_handle = con_handle;
    s_hogp_encrypted = 0;
    RDX_HOGP_LOG("conn complete hdl=0x%04x", con_handle);
#if RDX_HOGP_ENCRYPTION_REQUIRED
    sm_api_request_pairing(con_handle);
#endif
    rdx_hogp_dump_state();
}

void rdx_hogp_on_disconnected(u16 con_handle)
{
    (void)con_handle;
    if (!s_hogp_mode) {
        return;
    }
    hogp_cancel_key_up_timer();
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    RDX_HOGP_LOG("disconnect");
    rdx_hogp_dump_state();
}

void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status)
{
    RDX_HOGP_LOG("encryption_change hdl=0x%04x enabled=%d status=%d",
             con_handle, enabled, status);
    if (s_hogp_mode && con_handle == s_hid_con_handle && enabled && status == 0) {
        s_hogp_encrypted = 1;
        RDX_HOGP_LOG("link encrypted");
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
        if (s_hogp_mode) {
            RDX_HOGP_LOG("Just Works pairing request, confirm");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        }
        break;
    default:
        break;
    }
}

/******************************************************************************
* Advertising
******************************************************************************/
int rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len)
{
    u8 offset = 0;

    offset += make_eir_packet_val(&adv_data[offset], offset,
                                  HCI_EIR_DATATYPE_FLAGS, 0x06, 1);

    u8 hid_uuid[] = {0x12, 0x18};
    offset += make_eir_packet_data(&adv_data[offset], offset,
                                   HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS,
                                   hid_uuid, sizeof(hid_uuid));

    offset += make_eir_packet_val(&adv_data[offset], offset,
                                  HCI_EIR_DATATYPE_APPEARANCE_DATA, RDX_HOGP_APPEARANCE, 2);

#if RDX_HOGP_NAME_SOURCE == 0
    const char *name = rdx_ble_server_get_local_name();
#else
    const char *name = RDX_HOGP_CUSTOM_NAME;
#endif
    u8 name_len = (u8)strlen(name);
    if (name_len > max_len - offset - 2) {
        name_len = max_len - offset - 2;
    }
    offset += make_eir_packet_data(&adv_data[offset], offset,
                                   HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME,
                                   (u8 *)name, name_len);

    return offset;
}

void rdx_hogp_adv_start(void)
{
    if (s_hogp_mode) {
        hogp_adv_start_internal();
    }
}

void rdx_hogp_adv_stop(void)
{
    hogp_adv_stop_internal();
}

/******************************************************************************
* Key input
******************************************************************************/
int rdx_hogp_key_send_usage(u8 usage, u8 pressed)
{
    u8 report[8] = {0};

    if (pressed) {
        report[2] = usage;
    }

    RDX_HOGP_LOG("key_send usage=0x%02x pressed=%d report=%02x %02x %02x %02x %02x %02x %02x %02x conn=%d notify=%d encrypted=%d",
             usage, pressed,
             report[0], report[1], report[2], report[3],
             report[4], report[5], report[6], report[7],
             s_hogp_connected, s_hid_notify_enabled, s_hogp_encrypted);

    if (!s_hogp_connected) {
        RDX_HOGP_ERROR("key_send skipped: not connected");
        rdx_hogp_dump_state();
        return -1;
    }
    if (s_hogp_app_ble_hdl == NULL) {
        RDX_HOGP_ERROR("key_send skipped: server hdl NULL");
        rdx_hogp_dump_state();
        return -1;
    }
    if (!s_hid_notify_enabled) {
        RDX_HOGP_ERROR("key_send skipped: notify not enabled");
        rdx_hogp_dump_state();
        return -1;
    }
#if RDX_HOGP_ENCRYPTION_REQUIRED
    if (!s_hogp_encrypted) {
        RDX_HOGP_ERROR("key_send skipped: not encrypted");
        rdx_hogp_dump_state();
        return -1;
    }
#endif

    if (!rdx_ble_connection_owner_is_hogp()) {
        RDX_HOGP_ERROR("key_send skipped: not HOGP owner");
        rdx_hogp_dump_state();
        return -1;
    }

    int ret = app_ble_att_send_data(s_hogp_app_ble_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    report, sizeof(report),
                                    ATT_OP_NOTIFY);
    RDX_HOGP_LOG("key_send ret=%d", ret);
    if (ret != APP_BLE_NO_ERROR) {
        RDX_HOGP_ERROR("key_send failed ret=%d", ret);
        rdx_hogp_dump_state();
    }
    return ret;
}

int rdx_hogp_key_click_usage(u8 usage)
{
    int ret = rdx_hogp_key_send_usage(usage, 1);

    if (ret == APP_BLE_NO_ERROR) {
        hogp_cancel_key_up_timer();
        s_hogp_key_generation = s_hogp_generation;
        s_hogp_key_up_timer = sys_timeout_add((void *)(u32)usage, hogp_key_up_timeout, RDX_HOGP_KEY_UP_DELAY_MS);
    }

    return ret;
}

int rdx_hogp_key_click_index(u8 key_index)
{
    if (key_index >= 5) {
        return -1;
    }
    return rdx_hogp_key_click_usage(key_to_hid_usage[key_index]);
}

int rdx_hogp_on_io_num_key(u8 num_idx, u8 action)
{
#if RDX_BLE_DEBUG_MODE_SWITCH_KEY
    if (num_idx == 0 && action == KEY_ACTION_CLICK && !s_hogp_mode) {
        rdx_ble_mode_request_hogp(1);
        RDX_HOGP_LOG("enter HOGP mode");
        return 0;
    }
    if (num_idx == 0 && action == KEY_ACTION_LONG && s_hogp_mode) {
        rdx_ble_mode_request_hogp(0);
        RDX_HOGP_LOG("exit HOGP mode");
        return 0;
    }
#endif

    if (!s_hogp_mode) {
        return -1;
    }

    if (action == KEY_ACTION_CLICK) {
        return rdx_hogp_key_click_index(num_idx - 1);
    }

    return -1;
}

/******************************************************************************
* Legacy compatibility wrappers
******************************************************************************/
void hogp_mode_set(u8 enable)
{
    rdx_hogp_mode_set(enable);
}

u8 hogp_mode_get(void)
{
    return rdx_hogp_mode_get();
}

void hogp_key_send(u8 key_index, u8 pressed)
{
    if (key_index >= 5) {
        RDX_HOGP_ERROR("key_index %d out of range", key_index);
        return;
    }
    rdx_hogp_key_send_usage(key_to_hid_usage[key_index], pressed);
}

void hogp_key_click_send(u8 key_index)
{
    if (key_index >= 5) {
        return;
    }
    rdx_hogp_key_click_index(key_index);
}

#else  /* !(TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)) — stubs */

void rdx_hogp_init(void *app_ble_hdl) { (void)app_ble_hdl; }
void rdx_hogp_deinit(void) {}
void rdx_hogp_runtime_cleanup(void) {}
u8   rdx_hogp_mode_get(void) { return 0; }
void rdx_hogp_mode_set(u8 enable) { (void)enable; }
u8   rdx_hogp_is_handle(u16 att_handle) { (void)att_handle; return 0; }
u16  rdx_hogp_att_read(hci_con_handle_t ch, u16 h, u16 o, u8 *b, u16 bs) {
    (void)ch; (void)h; (void)o; (void)b; (void)bs; return 0;
}
int  rdx_hogp_att_write(hci_con_handle_t ch, u16 h, u16 tm, u16 o, u8 *b, u16 bs) {
    (void)ch; (void)h; (void)tm; (void)o; (void)b; (void)bs; return 0;
}
void rdx_hogp_on_connected(u16 con_handle) { (void)con_handle; }
void rdx_hogp_on_disconnected(u16 con_handle) { (void)con_handle; }
void rdx_hogp_on_encryption_change(u16 ch, u8 en, u8 st) { (void)ch; (void)en; (void)st; }
void rdx_hogp_on_sm_event(u8 pt, u8 *pk, u16 sz) { (void)pt; (void)pk; (void)sz; }
int  rdx_hogp_fill_adv_data(u8 *adv_data, u8 max_len) { (void)adv_data; (void)max_len; return 0; }
void rdx_hogp_adv_start(void) {}
void rdx_hogp_adv_stop(void) {}
int  rdx_hogp_key_send_usage(u8 usage, u8 pressed) { (void)usage; (void)pressed; return -1; }
int  rdx_hogp_key_click_usage(u8 usage) { (void)usage; return -1; }
int  rdx_hogp_key_click_index(u8 key_index) { (void)key_index; return -1; }
int  rdx_hogp_on_io_num_key(u8 num_idx, u8 action) { (void)num_idx; (void)action; return -1; }
void rdx_hogp_dump_state(void) {}

/* Legacy wrappers — stubs */
void hogp_mode_set(u8 enable) { (void)enable; }
u8   hogp_mode_get(void) { return 0; }
void hogp_key_send(u8 key_index, u8 pressed) { (void)key_index; (void)pressed; }
void hogp_key_click_send(u8 key_index) { (void)key_index; }

#endif /* TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */
