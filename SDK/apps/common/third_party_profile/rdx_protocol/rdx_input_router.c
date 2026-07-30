/* Typed, exclusive routing from the five physical keys to HID providers. */

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_input_router.data.bss")
#pragma data_seg(".rdx_input_router.data")
#pragma const_seg(".rdx_input_router.text.const")
#pragma code_seg(".rdx_input_router.text")
#endif

#include "app_config.h"
#include "rdx_app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hid_service.h"
#include "rdx_codex_micro.h"
#include "rdx_input_router.h"
#include "ble_user.h"
#include "device/hid/hid_keyboard_usage.h"

typedef char rdx_input_action_entry_size_check[
    (sizeof(rdx_input_action_entry_t) == 8) ? 1 : -1
];

#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
static const rdx_input_action_map_t s_rdx_input_test_map = {
    RDX_INPUT_ACTION_MAP_VERSION,
    RDX_INPUT_ROUTER_PHYSICAL_KEY_COUNT,
    {
        { RDX_INPUT_ACTION_KEYBOARD,
          { HID_KEYBOARD_MOD_LCTRL, HID_KEYBOARD_USAGE_C, 0, 0, 0, 0, 0 } },
        { RDX_INPUT_ACTION_KEYBOARD,
          { HID_KEYBOARD_MOD_LCTRL, HID_KEYBOARD_USAGE_V, 0, 0, 0, 0, 0 } },
        { RDX_INPUT_ACTION_KEYBOARD,
          { 0, HID_KEYBOARD_USAGE_BACKSPACE, 0, 0, 0, 0, 0 } },
        { RDX_INPUT_ACTION_KEYBOARD,
          { 0, HID_KEYBOARD_USAGE_ENTER, 0, 0, 0, 0, 0 } },
        { RDX_INPUT_ACTION_CODEX_FAST, { 0, 0, 0, 0, 0, 0, 0 } },
    },
};
#endif

static rdx_input_action_map_t s_rdx_input_active_map;
static u8 s_rdx_input_active;
static u8 s_rdx_keyboard_pressed;
static u16 s_rdx_keyboard_release_timer;

static void rdx_input_router_keyboard_release(void)
{
    if (s_rdx_keyboard_pressed && rdx_hogp_keyboard_is_ready()) {
        (void)rdx_hogp_keyboard_release_all();
    }
    s_rdx_keyboard_pressed = 0;
}

static void rdx_input_router_keyboard_release_timer_cb(void *priv)
{
    (void)priv;
    s_rdx_keyboard_release_timer = 0;
    rdx_input_router_keyboard_release();
}

static void rdx_input_router_keyboard_timer_cancel(void)
{
    if (s_rdx_keyboard_release_timer) {
        sys_timeout_del(s_rdx_keyboard_release_timer);
        s_rdx_keyboard_release_timer = 0;
    }
}

static void rdx_input_router_active_map_clear(void)
{
    memset(&s_rdx_input_active_map, 0, sizeof(s_rdx_input_active_map));
    s_rdx_input_active = 0;
}

static u8 rdx_input_router_data_is_zero(const u8 *data, u8 offset)
{
    u8 i;

    for (i = offset; i < RDX_INPUT_ACTION_DATA_LEN; i++) {
        if (data[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static u8 rdx_input_router_entry_is_valid(
    const rdx_input_action_entry_t *entry)
{
    switch (entry->kind) {
    case RDX_INPUT_ACTION_NONE:
        return rdx_input_router_data_is_zero(entry->data, 0);
    case RDX_INPUT_ACTION_KEYBOARD:
        return 1;
    case RDX_INPUT_ACTION_CODEX_FAST:
        return rdx_input_router_data_is_zero(entry->data, 0);
    default:
        return 0;
    }
}

static int rdx_input_router_keyboard_click(
    const rdx_input_action_entry_t *entry)
{
    rdx_hogp_keyboard_report_t report = {0};
    int ret;

    if (!rdx_hogp_keyboard_is_ready()) {
        return RDX_INPUT_ROUTER_NOT_SENT;
    }

    report.modifiers = entry->data[0];
    memcpy(report.usages, &entry->data[1], sizeof(report.usages));
    ret = rdx_hogp_keyboard_report_send(&report);
    if (ret != APP_BLE_NO_ERROR) {
        return RDX_INPUT_ROUTER_NOT_SENT;
    }

    s_rdx_keyboard_pressed = 1;
    rdx_input_router_keyboard_timer_cancel();
    s_rdx_keyboard_release_timer = sys_timeout_add(
        NULL, rdx_input_router_keyboard_release_timer_cb,
        TCFG_RDX_HOGP_KEY_UP_DELAY_MS);
    if (!s_rdx_keyboard_release_timer) {
        rdx_input_router_keyboard_release();
        return RDX_INPUT_ROUTER_NOT_SENT;
    }
    return RDX_INPUT_ROUTER_OK;
}

void rdx_input_router_init(void)
{
    s_rdx_keyboard_release_timer = 0;
    s_rdx_keyboard_pressed = 0;
    rdx_input_router_active_map_clear();

#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
    memcpy(&s_rdx_input_active_map,
           &s_rdx_input_test_map,
           sizeof(s_rdx_input_active_map));
    s_rdx_input_active = 1;
    y_printf("[RDX_INPUT_ROUTER] action_map_source=TEST\n");
#else
    y_printf("[RDX_INPUT_ROUTER] action_map_source=APP_VM_V1\n");
#endif
}

void rdx_input_router_keyboard_ready_drop_cleanup(void)
{
    rdx_input_router_keyboard_timer_cancel();
    rdx_input_router_keyboard_release();
}

void rdx_input_router_reset(void)
{
    rdx_input_router_keyboard_ready_drop_cleanup();
    rdx_codex_micro_fast_key_release_all();
}

void rdx_input_router_deinit(void)
{
    rdx_input_router_reset();
    rdx_input_router_active_map_clear();
}

int rdx_input_router_action_map_apply(
    const rdx_input_action_map_t *action_map)
{
    u8 key_id;

#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
    (void)action_map;
    return RDX_INPUT_ROUTER_TEST_MODE;
#else
    if (!action_map ||
        action_map->version != RDX_INPUT_ACTION_MAP_VERSION ||
        action_map->key_count != RDX_INPUT_ROUTER_PHYSICAL_KEY_COUNT) {
        return RDX_INPUT_ROUTER_INVALID;
    }
    for (key_id = 0; key_id < action_map->key_count; key_id++) {
        if (!rdx_input_router_entry_is_valid(&action_map->entries[key_id])) {
            return RDX_INPUT_ROUTER_INVALID;
        }
    }

    /* Release old provider state before publishing the replacement map. */
    rdx_input_router_reset();
    memset(&s_rdx_input_active_map, 0, sizeof(s_rdx_input_active_map));
    memcpy(&s_rdx_input_active_map, action_map, sizeof(*action_map));
    s_rdx_input_active = 1;
    return RDX_INPUT_ROUTER_OK;
#endif
}

int rdx_input_router_click(u8 physical_key_id)
{
    const rdx_input_action_entry_t *entry;

    if (!s_rdx_input_active ||
        physical_key_id >= RDX_INPUT_ROUTER_PHYSICAL_KEY_COUNT ||
        physical_key_id >= s_rdx_input_active_map.key_count) {
        return RDX_INPUT_ROUTER_INVALID;
    }

    entry = &s_rdx_input_active_map.entries[physical_key_id];
    switch (entry->kind) {
    case RDX_INPUT_ACTION_NONE:
        return RDX_INPUT_ROUTER_OK;
    case RDX_INPUT_ACTION_KEYBOARD:
        return rdx_input_router_keyboard_click(entry);
    case RDX_INPUT_ACTION_CODEX_FAST:
        if (!rdx_hid_report_is_ready(RDX_HID_REPORT_CODEX)) {
            return RDX_INPUT_ROUTER_NOT_SENT;
        }
        return rdx_codex_micro_fast_key_click() ?
               RDX_INPUT_ROUTER_NOT_SENT : RDX_INPUT_ROUTER_OK;
    default:
        return RDX_INPUT_ROUTER_INVALID;
    }
}

u8 rdx_input_router_test_mode_active(void)
{
#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
    return 1;
#else
    return 0;
#endif
}
