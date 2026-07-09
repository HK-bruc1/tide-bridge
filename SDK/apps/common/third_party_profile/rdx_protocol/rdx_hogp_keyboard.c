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
#include "rdx_hogp_keyboard.h"

#include "sdk_config.h"
#include "app_config.h"
#include "system/includes.h"
#include "ble_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btstack/btstack_event.h"
#include "multi_protocol_main.h"

#include "rdx_ble_server.h"
#include "rdx_app.h"
#include "rdx_util.h"
#include "rdx_commonDef.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG                                     "[rdx_hogp]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

/******************************************************************************
* Local Macro Define Section
******************************************************************************/
/* Temporary handle macros — will be centralized in rdx_hogp_profile.h in Phase 2 */
#define HID_SERVICE_HANDLE                                              0x0016
#define HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE                         0x0017
#define HID_PROTOCOL_MODE_VALUE_HANDLE                                  0x0018
#define HID_INPUT_REPORT_CHARACTERISTIC_HANDLE                          0x0019
#define HID_INPUT_REPORT_VALUE_HANDLE                                   0x001a
#define HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE                    0x001b
#define HID_INPUT_REPORT_REFERENCE_HANDLE                               0x001c
#define HID_REPORT_MAP_CHARACTERISTIC_HANDLE                            0x001d
#define HID_REPORT_MAP_VALUE_HANDLE                                     0x001e
#define HID_INFORMATION_CHARACTERISTIC_HANDLE                           0x001f
#define HID_INFORMATION_VALUE_HANDLE                                    0x0020
#define HID_CONTROL_POINT_CHARACTERISTIC_HANDLE                         0x0021
#define HID_CONTROL_POINT_VALUE_HANDLE                                  0x0022
#define HID_OUTPUT_REPORT_VALUE_HANDLE                                  0x0029

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

/* Default 5-key keymap (A, B, C, D, E) — will become configurable in Phase 3 */
static const u8 key_to_hid_usage[5] = {
    0x04,  // A
    0x05,  // B
    0x06,  // C
    0x07,  // D
    0x08,  // E
};

static const u8 hid_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0xFF,        //   Usage Maximum (255)
    0x81, 0x00,        //   Input (Data, Array)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x03,        //   Usage Maximum (Scroll Lock)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x03,        //   Report Count (3)
    0x75, 0x01,        //   Report Size (1)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x05,        //   Report Size (5)
    0x91, 0x01,        //   Output (Constant)
    0xC0               // End Collection
};

static const u8 hid_information[] = {0x11, 0x01, 0x00, 0x03};
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

static void hogp_key_up_timeout(void *priv)
{
    u8 usage = (u8)(u32)priv;
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

    y_printf("[HOGP] HID advertising started\r");
}

static void hogp_adv_stop_internal(void)
{
    if (s_hogp_app_ble_hdl == NULL) {
        return;
    }

    app_ble_adv_enable(s_hogp_app_ble_hdl, 0);
    rdx_ble_server_adv_enable(1);

    y_printf("[HOGP] HID advertising stopped, restore RDX advertising\r");
}

/******************************************************************************
* Lifecycle
******************************************************************************/
void rdx_hogp_init(void *app_ble_hdl)
{
    s_hogp_app_ble_hdl = app_ble_hdl;
    s_hogp_mode = 0;
    s_hogp_connected = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    s_hid_con_handle = 0;
    memset((void *)s_hid_input_report, 0, sizeof(s_hid_input_report));
}

void rdx_hogp_deinit(void)
{
    rdx_hogp_mode_set(0);
    s_hogp_app_ble_hdl = NULL;
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
        y_printf("[HOGP] active ble conn, disconnect before mode switch\r");
        rdx_ble_server_app_disconnect();
    }

    if (new_mode) {
        s_hogp_mode = 1;
        hogp_adv_start_internal();
    } else {
        hogp_adv_stop_internal();
        s_hogp_mode = 0;
        s_hogp_connected = 0;
        s_hid_con_handle = 0;
        s_hid_notify_enabled = 0;
        s_hogp_encrypted = 0;
    }
}

/******************************************************************************
* ATT routing
******************************************************************************/
u8 rdx_hogp_is_handle(u16 att_handle)
{
    return (att_handle >= HID_SERVICE_HANDLE && att_handle <= HID_CONTROL_POINT_VALUE_HANDLE) ? 1 : 0;
}

u16 rdx_hogp_att_read(hci_con_handle_t connection_handle,
                      u16 att_handle,
                      u16 offset,
                      u8 *buffer,
                      u16 buffer_size)
{
    (void)connection_handle;

    switch (att_handle) {
    case HID_PROTOCOL_MODE_VALUE_HANDLE:
        return hid_read_helper(&hid_protocol_mode, 1, offset, buffer, buffer_size);
    case HID_REPORT_MAP_VALUE_HANDLE:
        return hid_read_helper(hid_report_map, sizeof(hid_report_map), offset, buffer, buffer_size);
    case HID_INFORMATION_VALUE_HANDLE:
        return hid_read_helper(hid_information, sizeof(hid_information), offset, buffer, buffer_size);
    case HID_INPUT_REPORT_VALUE_HANDLE:
        return hid_read_helper(s_hid_input_report, sizeof(s_hid_input_report), offset, buffer, buffer_size);
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
    (void)transaction_mode;
    (void)offset;

    switch (att_handle) {
    case HID_CONTROL_POINT_VALUE_HANDLE:
        if (buffer_size >= 1) {
            y_printf("[HOGP] ctrl point hdl=0x%04x val=0x%02x\r", att_handle, buffer[0]);
        }
        return 0;
    case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:
        if (buffer_size >= 2) {
            u16 cfg = buffer[0] | (buffer[1] << 8);
            s_hid_notify_enabled = (cfg & 0x01);
            multi_att_set_ccc_config(connection_handle, att_handle, cfg);
            y_printf("[HOGP] CCC write hdl=0x%04x cfg=0x%04x notify=%d\r", att_handle, cfg, s_hid_notify_enabled);
        }
        return 0;
    case HID_INPUT_REPORT_VALUE_HANDLE:
        y_printf("[HOGP] input report write hdl=0x%04x len=%d data[0]=0x%02x\r",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    case HID_OUTPUT_REPORT_VALUE_HANDLE:
        if (buffer_size >= 1) {
            y_printf("[HOGP] output report write, LED=0x%02x\r", buffer[0]);
        }
        return 0;
    default:
        y_printf("[HOGP] write default hdl=0x%04x len=%d data[0]=0x%02x\r",
                 att_handle, buffer_size, buffer_size ? buffer[0] : 0);
        return 0;
    }
}

/******************************************************************************
* Connection / security events
******************************************************************************/
void rdx_hogp_on_connected(u16 con_handle)
{
    if (!s_hogp_mode) {
        return;
    }
    s_hogp_connected = 1;
    s_hid_con_handle = con_handle;
    s_hogp_encrypted = 0;
    y_printf("[HOGP] conn complete hdl=0x%04x\r", con_handle);
    sm_api_request_pairing(con_handle);
}

void rdx_hogp_on_disconnected(u16 con_handle)
{
    (void)con_handle;
    if (!s_hogp_mode) {
        return;
    }
    s_hogp_connected = 0;
    s_hid_con_handle = 0;
    s_hid_notify_enabled = 0;
    s_hogp_encrypted = 0;
    y_printf("[HOGP] disconnect\r");
}

void rdx_hogp_on_encryption_change(u16 con_handle, u8 enabled, u8 status)
{
    y_printf("[HOGP] encryption_change hdl=0x%04x enabled=%d status=%d\r",
             con_handle, enabled, status);
    if (s_hogp_mode && con_handle == s_hid_con_handle && enabled && status == 0) {
        s_hogp_encrypted = 1;
        y_printf("[HOGP] link encrypted\r");
    }
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
            y_printf("[HOGP] Just Works pairing request, confirm\r");
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
                                  HCI_EIR_DATATYPE_APPEARANCE_DATA, 0x03C1, 2);

    const char *name = rdx_ble_server_get_local_name();
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

    y_printf("[HOGP] key_send usage=0x%02x pressed=%d report=%02x %02x %02x %02x %02x %02x %02x %02x conn=%d notify=%d encrypted=%d\r",
             usage, pressed,
             report[0], report[1], report[2], report[3],
             report[4], report[5], report[6], report[7],
             s_hogp_connected, s_hid_notify_enabled, s_hogp_encrypted);

    if (!s_hogp_connected) {
        y_printf("[HOGP] key_send skipped: not connected\r");
        return -1;
    }
    if (s_hogp_app_ble_hdl == NULL) {
        y_printf("[HOGP] key_send skipped: server hdl NULL\r");
        return -1;
    }
    if (!s_hid_notify_enabled) {
        y_printf("[HOGP] key_send skipped: notify not enabled\r");
        return -1;
    }
    if (!s_hogp_encrypted) {
        y_printf("[HOGP] key_send skipped: not encrypted\r");
        return -1;
    }

    int ret = app_ble_att_send_data(s_hogp_app_ble_hdl,
                                    HID_INPUT_REPORT_VALUE_HANDLE,
                                    report, sizeof(report),
                                    ATT_OP_NOTIFY);
    y_printf("[HOGP] key_send ret=%d\r", ret);
    return ret;
}

int rdx_hogp_key_click_usage(u8 usage)
{
    int ret = rdx_hogp_key_send_usage(usage, 1);
    sys_timeout_add((void *)(u32)usage, hogp_key_up_timeout, 20);
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
    if (!s_hogp_mode) {
        if (num_idx == 0 && action == KEY_ACTION_CLICK) {
            rdx_hogp_mode_set(1);
            y_printf("[HOGP] enter HOGP mode\r");
            return 0;
        }
        return -1;
    }

    if (num_idx == 0) {
        if (action == KEY_ACTION_LONG) {
            rdx_hogp_mode_set(0);
            y_printf("[HOGP] exit HOGP mode\r");
            return 0;
        }
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
        y_printf("[HOGP] err: key_index %d out of range\r", key_index);
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

#endif /* (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) */
