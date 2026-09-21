/*=====================================================================================
 HEADER NAME: rdx_hogp_key_action.c
 MODULE NAME: RDX HOGP key action executor.

 GENERAL DESCRIPTION:
    Minimal active-keymap executor for full HOGP keyboard reports. Owns the built-in
    test keymap, private RAM active keymap, held report and release barrier.
    Does not control advertising, connections, HFP, or VM.

=======================================================================================*/

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_key_action.data.bss")
#pragma data_seg(".rdx_hogp_key_action.data")
#pragma const_seg(".rdx_hogp_key_action.text.const")
#pragma code_seg(".rdx_hogp_key_action.text")
#endif

/******************************************************************************
* Include files
******************************************************************************/
#include "app_config.h"
#include "rdx_app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_key_action.h"
#include "rdx_hogp_input.h"
#include "device/hid/hid_keyboard_usage.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/
#define RDX_HOGP_KEY_ACTION_KEYMAP_VERSION       1

/******************************************************************************
* Local Variables Section
******************************************************************************/
#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
static const rdx_hogp_key_action_keyboard_t s_rdx_hogp_test_keymap[RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT] = {
    { HID_KEYBOARD_MOD_LCTRL, { HID_KEYBOARD_USAGE_C, 0, 0, 0, 0, 0 } },
    { HID_KEYBOARD_MOD_LCTRL, { HID_KEYBOARD_USAGE_V, 0, 0, 0, 0, 0 } },
    { HID_KEYBOARD_MOD_LCTRL, { HID_KEYBOARD_USAGE_X, 0, 0, 0, 0, 0 } },
    { 0, { HID_KEYBOARD_USAGE_BACKSPACE, 0, 0, 0, 0, 0 } },
    { 0, { HID_KEYBOARD_USAGE_ENTER, 0, 0, 0, 0, 0 } },
};
#endif

static rdx_hogp_key_action_keymap_t s_rdx_hogp_key_action_active_keymap = {0};
static u8 s_rdx_hogp_key_action_active = 0;
/* app_core owns the action state. A failed Up remains owed until accepted
 * or until its independent HID token is invalidated. */
static rdx_hogp_token_t held_token;
static rdx_hogp_keyboard_report_t held_report;
static u8 token_valid;
static u8 held_key = 0xff;
static u8 release_pending;
static u8 down_pending, send_pending;
static u32 held_input_epoch;
static u32 map_generation;
static u32 release_failures;

static u8 same_token(const rdx_hogp_token_t *a, const rdx_hogp_token_t *b)
{
    return a->hid_epoch == b->hid_epoch &&
           a->slot_generation == b->slot_generation &&
           a->slot_index == b->slot_index;
}

void rdx_hogp_key_action_service(void)
{
    rdx_hogp_token_t current;
    rdx_hogp_keyboard_report_t zero = {0};
    int ret;
    u8 valid = rdx_hogp_token_capture(&current);
    if (!valid) {
        token_valid = 0;
        down_pending = send_pending = 0;
        held_key = 0xff;
        release_pending = 0;
        return;
    }
    if (!token_valid || !same_token(&current, &held_token)) {
        down_pending = send_pending = 0;
        held_token = current;
        token_valid = 1;
        held_key = 0xff;
        release_pending = 1; /* New session has its own zero-state barrier. */
        rdx_hogp_input_invalidate();
    }
    if (down_pending) {
        ret = rdx_hogp_report_send_for_input(&held_report, &held_token, held_input_epoch);
        send_pending = ret == RDX_HOGP_SEND_PENDING;
        if (send_pending) return;
        down_pending = 0;
        if (ret != APP_BLE_NO_ERROR) held_key = 0xff;
    }
    if (release_pending) {
        ret = rdx_hogp_report_send_for_token(&zero, &held_token);
        send_pending = ret == RDX_HOGP_SEND_PENDING;
        if (send_pending) return;
        if (ret == APP_BLE_NO_ERROR) {
            release_pending = 0;
            held_key = 0xff;
            memset(&held_report, 0, sizeof(held_report));
            release_failures = 0;
        } else if ((++release_failures & 127) == 1) {
            y_printf("[HOGP_KEY_ACTION] release blocked, attempts=%u\n", release_failures);
        }
    }
}

u8 rdx_hogp_key_action_busy(void)
{
    return send_pending;
}

u8 rdx_hogp_key_action_blocked(void)
{
    return release_pending;
}

u32 rdx_hogp_key_action_generation(void)
{
    return map_generation;
}

void rdx_hogp_key_action_release(u8 key_id)
{
    if (held_key == key_id) {
        release_pending = 1;
        rdx_hogp_key_action_service();
    }
}

void rdx_hogp_key_action_cancel(void)
{
    if (held_key != 0xff) release_pending = 1;
    /* Repeated apply/rollback never erases an outstanding release. */
    rdx_hogp_key_action_service();
}

static void rdx_hogp_key_action_clear_active_keymap(void)
{
    memset(&s_rdx_hogp_key_action_active_keymap, 0, sizeof(s_rdx_hogp_key_action_active_keymap));
    s_rdx_hogp_key_action_active = 0;
}

#if !(RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
static void rdx_hogp_key_action_load_default_keymap(void)
{
    /* Default keymap is intentionally empty until the BLE App keymap protocol
     * and VM persistence ABI are defined. The app-level HID connection router
     * decides between this executor and the offline RDX key table. */
    rdx_hogp_key_action_clear_active_keymap();
}
#endif

#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
static void rdx_hogp_key_action_load_test_keymap(void)
{
    rdx_hogp_key_action_clear_active_keymap();

    memcpy(s_rdx_hogp_key_action_active_keymap.keys,
           s_rdx_hogp_test_keymap,
           sizeof(s_rdx_hogp_test_keymap));
    s_rdx_hogp_key_action_active_keymap.version = RDX_HOGP_KEY_ACTION_KEYMAP_VERSION;
    s_rdx_hogp_key_action_active_keymap.key_count = RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT;
    s_rdx_hogp_key_action_active = 1;
}
#endif

static void rdx_hogp_key_action_to_keyboard_report(
    const rdx_hogp_key_action_keyboard_t *action,
    rdx_hogp_keyboard_report_t *report)
{
    report->modifiers = action->modifiers;
    report->reserved = 0;
    memcpy(report->usages, action->usages, sizeof(report->usages));
}

static u8 rdx_hogp_key_action_is_disabled(
    const rdx_hogp_key_action_keyboard_t *action)
{
    u8 i;

    if (action->modifiers != 0) {
        return 0;
    }
    for (i = 0; i < sizeof(action->usages); i++) {
        if (action->usages[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/******************************************************************************
* Public Function Section
******************************************************************************/
void rdx_hogp_key_action_init(void)
{
    token_valid = 0;
    down_pending = send_pending = 0;
    held_key = 0xff;
    release_pending = 0;

#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
    rdx_hogp_key_action_load_test_keymap();
#else
    rdx_hogp_key_action_load_default_keymap();
#endif
}

void rdx_hogp_key_action_reset(void)
{
    rdx_hogp_input_invalidate();
    rdx_hogp_key_action_cancel();
}

void rdx_hogp_key_action_deinit(void)
{
    rdx_hogp_key_action_reset();
    rdx_hogp_key_action_clear_active_keymap();
}

int rdx_hogp_key_action_keymap_apply(const rdx_hogp_key_action_keymap_t *keymap)
{
    if (keymap == NULL) {
        return -1;
    }

    if (keymap->version != RDX_HOGP_KEY_ACTION_KEYMAP_VERSION) {
        y_printf("[HOGP_KEY_ACTION] keymap version mismatch: %d\n", keymap->version);
        return -1;
    }

    if (keymap->key_count == 0 || keymap->key_count > RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT) {
        y_printf("[HOGP_KEY_ACTION] keymap count invalid: %d\n", keymap->key_count);
        return -1;
    }

    /* Do not let a key-up from the previous map race the replacement. */
    rdx_hogp_input_invalidate();
    rdx_hogp_key_action_cancel();
    ++map_generation;
    memset(&s_rdx_hogp_key_action_active_keymap, 0, sizeof(s_rdx_hogp_key_action_active_keymap));
    s_rdx_hogp_key_action_active_keymap.version = keymap->version;
    s_rdx_hogp_key_action_active_keymap.key_count = keymap->key_count;
    memcpy(s_rdx_hogp_key_action_active_keymap.keys,
           keymap->keys,
           keymap->key_count * sizeof(keymap->keys[0]));
    s_rdx_hogp_key_action_active = 1;
    return 0;
}

int rdx_hogp_key_action_press(u8 key_id, u32 input_epoch)
{
    rdx_hogp_keyboard_report_t report = {0};
    rdx_hogp_key_action_keyboard_t *action;
    int ret;

    if (!token_valid || release_pending || held_key != 0xff) return -1;

    if (key_id >= RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT) {
        return -1;
    }

    if (!s_rdx_hogp_key_action_active) {
        return -1;
    }

    if (key_id >= s_rdx_hogp_key_action_active_keymap.key_count) {
        return -1;
    }

    action = &s_rdx_hogp_key_action_active_keymap.keys[key_id];

    /* A disabled physical key is consumed by the HOGP owner but must not emit
     * either a key-down or a redundant all-zero release report. */
    if (rdx_hogp_key_action_is_disabled(action)) {
        return 0;
    }

    rdx_hogp_key_action_to_keyboard_report(action, &report);

    ret = rdx_hogp_report_send_for_input(&report, &held_token, input_epoch);
    if (ret != APP_BLE_NO_ERROR && ret != RDX_HOGP_SEND_PENDING) {
        y_printf("[HOGP_KEY_ACTION] key %d send failed: %d\n", key_id, ret);
        return 1;   /* consumed but not sent */
    }

    held_report = report;
    held_key = key_id;
    held_input_epoch = input_epoch;
    down_pending = send_pending = ret == RDX_HOGP_SEND_PENDING;
    return 0;
}
