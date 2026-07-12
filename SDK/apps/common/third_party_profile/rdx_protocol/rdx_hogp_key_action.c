/*=====================================================================================
 HEADER NAME: rdx_hogp_key_action.c
 MODULE NAME: RDX HOGP key action executor.

 GENERAL DESCRIPTION:
    Minimal active-keymap executor for full HOGP keyboard reports. Owns the built-in
    test keymap, private RAM active keymap, and key-up release timer.
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

/******************************************************************************
* Macro Define Section
******************************************************************************/
#define RDX_HOGP_KEY_ACTION_KEYMAP_VERSION       1

/******************************************************************************
* Local Variables Section
******************************************************************************/
#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
static const rdx_hogp_key_action_keyboard_t s_rdx_hogp_test_keymap[RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT] = {
    { 0x01, { 0x19, 0x00, 0x00, 0x00, 0x00, 0x00 } }, /* KEY1: Ctrl+V */
    { 0x00, { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 } }, /* KEY2: A */
    { 0x00, { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00 } }, /* KEY3: Enter */
    { 0x01, { 0x06, 0x00, 0x00, 0x00, 0x00, 0x00 } }, /* KEY4: Ctrl+C */
    { 0x00, { 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00 } }, /* KEY5: Backspace */
};
#endif

static rdx_hogp_key_action_keymap_t s_rdx_hogp_key_action_active_keymap = {0};
static u8 s_rdx_hogp_key_action_active = 0;
static u16 s_rdx_hogp_key_action_release_timer = 0;

/******************************************************************************
* Local Function Section
******************************************************************************/
static void rdx_hogp_key_action_release_timer_cb(void *priv)
{
    (void)priv;
    s_rdx_hogp_key_action_release_timer = 0;
    rdx_hogp_keyboard_release_all();
}

static void rdx_hogp_key_action_cancel_release_timer(void)
{
    if (s_rdx_hogp_key_action_release_timer) {
        sys_timeout_del(s_rdx_hogp_key_action_release_timer);
        s_rdx_hogp_key_action_release_timer = 0;
    }
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

/******************************************************************************
* Public Function Section
******************************************************************************/
void rdx_hogp_key_action_init(void)
{
    s_rdx_hogp_key_action_release_timer = 0;

#if (RDX_HOGP_KEY_ACTION_TEST_ENABLE && TCFG_RDX_HOGP_ENABLE)
    rdx_hogp_key_action_load_test_keymap();
#else
    rdx_hogp_key_action_load_default_keymap();
#endif
}

void rdx_hogp_key_action_reset(void)
{
    rdx_hogp_key_action_cancel_release_timer();
    /* Attempt a clean release in case a key-down is still pending. */
    rdx_hogp_keyboard_release_all();
}

void rdx_hogp_key_action_deinit(void)
{
    rdx_hogp_key_action_cancel_release_timer();
    rdx_hogp_keyboard_release_all();

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

    memset(&s_rdx_hogp_key_action_active_keymap, 0, sizeof(s_rdx_hogp_key_action_active_keymap));
    s_rdx_hogp_key_action_active_keymap.version = keymap->version;
    s_rdx_hogp_key_action_active_keymap.key_count = keymap->key_count;
    memcpy(s_rdx_hogp_key_action_active_keymap.keys,
           keymap->keys,
           keymap->key_count * sizeof(keymap->keys[0]));
    s_rdx_hogp_key_action_active = 1;
    return 0;
}

int rdx_hogp_key_action_click(u8 key_id)
{
    rdx_hogp_keyboard_report_t report = {0};
    rdx_hogp_key_action_keyboard_t *action;
    int ret;

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

    rdx_hogp_key_action_to_keyboard_report(action, &report);

    ret = rdx_hogp_keyboard_report_send(&report);
    if (ret != APP_BLE_NO_ERROR) {
        y_printf("[HOGP_KEY_ACTION] key %d send failed: %d\n", key_id, ret);
        return 1;   /* consumed but not sent */
    }

    rdx_hogp_key_action_cancel_release_timer();
    s_rdx_hogp_key_action_release_timer =
        sys_timeout_add(NULL, rdx_hogp_key_action_release_timer_cb, TCFG_RDX_HOGP_KEY_UP_DELAY_MS);
    if (s_rdx_hogp_key_action_release_timer == 0) {
        y_printf("[HOGP_KEY_ACTION] key %d release timer failed, send immediate release\n", key_id);
        rdx_hogp_keyboard_release_all();
        return 1;   /* consumed but release timer could not be started */
    }

    return 0;   /* consumed and sent */
}
