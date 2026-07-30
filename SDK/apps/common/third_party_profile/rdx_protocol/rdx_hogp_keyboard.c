#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_keyboard.data.bss")
#pragma data_seg(".rdx_hogp_keyboard.data")
#pragma const_seg(".rdx_hogp_keyboard.text.const")
#pragma code_seg(".rdx_hogp_keyboard.text")
#endif

#include "sdk_config.h"
#include "app_config.h"

#include "rdx_hogp_keyboard.h"
#include "rdx_hid_service.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_profile.h"
#include "rdx_gatt_profile.h"
#include "rdx_input_router.h"
#include "ble_user.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#if RDX_HOGP_LOG_ENABLE
#define RDX_HOGP_LOG(fmt, ...)      y_printf("[HOGP_KBD] " fmt "\r", ##__VA_ARGS__)
#define RDX_HOGP_ERROR(fmt, ...)    y_printf("[HOGP_KBD_ERR] " fmt "\r", ##__VA_ARGS__)
#else
#define RDX_HOGP_LOG(fmt, ...)
#define RDX_HOGP_ERROR(fmt, ...)
#endif

#define RDX_HOGP_ATT_ERR_INVALID_OFFSET                 0x07
#define RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN    0x0d

static rdx_hogp_keyboard_report_t s_hid_input_report;
static u8 s_hid_output_report;

static u16 rdx_hogp_keyboard_read_helper(const u8 *data, u16 data_len,
                                         u16 offset, u8 *buffer,
                                         u16 buffer_size)
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

void rdx_hogp_keyboard_runtime_reset(void)
{
    memset(&s_hid_input_report, 0, sizeof(s_hid_input_report));
    s_hid_output_report = RDX_HOGP_OUTPUT_REPORT_DEFAULT_VALUE;
}

void rdx_hogp_keyboard_ready_drop_cleanup(void)
{
    rdx_input_router_keyboard_ready_drop_cleanup();
    memset(&s_hid_input_report, 0, sizeof(s_hid_input_report));
}

u16 rdx_hogp_keyboard_att_read(u16 att_handle, u16 offset,
                               u8 *buffer, u16 buffer_size)
{
    if (att_handle == HID_INPUT_REPORT_VALUE_HANDLE) {
        return rdx_hogp_keyboard_read_helper(
            (const u8 *)&s_hid_input_report, RDX_HOGP_KEYBOARD_REPORT_LEN,
            offset, buffer, buffer_size);
    }
    if (att_handle == HID_OUTPUT_REPORT_VALUE_HANDLE) {
        return rdx_hogp_keyboard_read_helper(
            &s_hid_output_report, 1, offset, buffer, buffer_size);
    }
    return 0;
}

int rdx_hogp_keyboard_att_write(u16 offset, const u8 *buffer,
                                u16 buffer_size)
{
    if (offset != 0) {
        return RDX_HOGP_ATT_ERR_INVALID_OFFSET;
    }
    if (!buffer || buffer_size != 1) {
        return RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN;
    }
    s_hid_output_report = buffer[0];
    RDX_HOGP_LOG("output report LED=0x%02x", s_hid_output_report);
    return 0;
}

u8 rdx_hogp_keyboard_is_ready(void)
{
    return rdx_hid_report_is_ready(RDX_HID_REPORT_KEYBOARD);
}

int rdx_hogp_keyboard_report_send(
    const rdx_hogp_keyboard_report_t *report)
{
    u8 payload[RDX_HOGP_KEYBOARD_REPORT_LEN];
    int ret;

    if (!report) {
        return -1;
    }
    memcpy(payload, report, sizeof(payload));
    ret = rdx_hid_report_notify(RDX_HID_REPORT_KEYBOARD,
                                HID_INPUT_REPORT_VALUE_HANDLE,
                                payload, sizeof(payload));
    if (ret == APP_BLE_NO_ERROR) {
        memcpy(&s_hid_input_report, payload, sizeof(s_hid_input_report));
    } else {
        RDX_HOGP_ERROR("report_send failed ret=%d", ret);
    }
    return ret;
}

int rdx_hogp_keyboard_release_all(void)
{
    rdx_hogp_keyboard_report_t report = {0};
    return rdx_hogp_keyboard_report_send(&report);
}

#else

void rdx_hogp_keyboard_runtime_reset(void) {}
void rdx_hogp_keyboard_ready_drop_cleanup(void) {}
u16 rdx_hogp_keyboard_att_read(u16 h, u16 o, u8 *b, u16 s)
{ (void)h; (void)o; (void)b; (void)s; return 0; }
int rdx_hogp_keyboard_att_write(u16 o, const u8 *b, u16 s)
{ (void)o; (void)b; (void)s; return -1; }
int rdx_hogp_keyboard_report_send(const rdx_hogp_keyboard_report_t *r)
{ (void)r; return -1; }
int rdx_hogp_keyboard_release_all(void) { return -1; }
u8 rdx_hogp_keyboard_is_ready(void) { return 0; }

#endif
