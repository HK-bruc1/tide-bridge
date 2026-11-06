#include "app_config.h"
#include "system/includes.h"
#include "app_main.h"
#include "audio_config.h"
#include "a2dp_player.h"
#include "btstack/avctp_user.h"
#include "dev_flow_player.h"
#include "tone_player.h"
#include "ring_player.h"
#include "volume_node.h"
#include "rdx_app.h"
#include "rdx_app_config.h"
#include "rdx_ble_server.h"
#include "rdx_ble_session.h"
#include "rdx_dip_switch.h"
#include "rdx_dut.h"
#include "rdx_factory_audio.h"
#include "rdx_protocol.h"
#include "rdx_record.h"
#include "rdx_record_format.h"
#include "rdx_record_tone.h"
#include "rdx_storage_lifecycle.h"
#include "rdx_uxfile.h"
#include "rdx_volume.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define RDX_VOLUME_NOTIFY_DELAY_MS 150

enum {
    RDX_VOLUME_ARG,
    RDX_VOLUME_GET,
    RDX_VOLUME_SET,
};

typedef struct {
    rdx_ble_async_token_t token;
    char id[6];
    u16 level;
    u8 op;
} rdx_volume_request_t;

typedef struct {
    rdx_ble_async_token_t token;
    u8 valid;
} rdx_volume_notify_t;

static rdx_volume_notify_t *volume_notify_pending;
static u32 volume_notify_cancel_serial;

extern bool rdx_record_process_is_busy_check(void);
extern bool rdx_uxfile_sd_format_status_check(void);
extern u8 rdx_uxfile_is_datFileInfo_loading(void);
extern u8 rdx_uxfile_is_index_active(void);
extern u8 get_ota_status(void);
extern ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);

static u8 rdx_volume_token_equal(const rdx_ble_async_token_t *left,
                                 const rdx_ble_async_token_t *right)
{
    return left && right &&
           left->slot_index == right->slot_index &&
           left->slot_generation == right->slot_generation &&
           left->transport_epoch == right->transport_epoch;
}

static u8 rdx_volume_token_current(const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_rdx_token_resolve(token, 1);
    return link && link->rdx_ccc_configured;
}

int rdx_volume_playback_max_get(u16 *max_level)
{
    struct volume_cfg cfg;

    if (!max_level ||
        volume_ioc_get_cfg(DEV_FLOW_PLAYER_VOLUME_NODE_NAME, &cfg) != 0 ||
        cfg.cfg_level_max < 1 || cfg.cfg_level_max > 99) {
        return -1;
    }
    *max_level = cfg.cfg_level_max;
    return 0;
}

static u8 rdx_volume_read_range(s16 *current, u16 *max_level)
{
    s16 cur;
    u16 max;

    if (!current || !max_level || rdx_volume_playback_max_get(&max)) {
        return 0;
    }
    cur = app_audio_get_volume(APP_AUDIO_STATE_MUSIC);
    if (cur < 0 || cur > max) {
        return 0;
    }
    *current = cur;
    *max_level = max;
    return 1;
}

static u8 rdx_volume_storage_busy(void)
{
    ReqFileInfo *file_info = rdx_protocol_get_uploadfileInfo();

    return rdx_factory_audio_file_busy() ||
           rdx_storage_lifecycle_business_blocked() ||
           app_in_mode(APP_MODE_PC) ||
           (file_info && file_info->file_send_busy) ||
           rdx_is_file_transfer_active() ||
           rdx_is_file_sync_busy() ||
           rdx_uxfile_is_datFileInfo_loading() ||
           rdx_uxfile_is_index_active() ||
           rdx_uxfile_is_formatting() ||
           rdx_uxfile_sd_format_status_check() ||
           rdx_record_format_service_status();
}

static u8 rdx_volume_common_busy(void)
{
    return app_var.goto_poweroff_flag ||
           rdx_app_get_poweroff_flag() ||
           rdx_dip_switch_shutdown_pending() ||
           get_ota_status() ||
           rdx_dut_is_in_mode() ||
           rdx_dut_test_keys_active() ||
           rdx_factory_audio_owned() ||
           rdx_volume_storage_busy();
}

static u8 rdx_volume_set_busy(void)
{
    u8 state = app_audio_get_state();

    return (state != APP_AUDIO_STATE_IDLE &&
            state != APP_AUDIO_STATE_MUSIC) ||
           rdx_record_config_busy() ||
           rdx_record_process_is_busy_check() ||
           a2dp_player_runing() ||
           bt_get_esco_coder_busy_flag() ||
           ring_player_runing() ||
           key_tone_player_running();
}

/* Recover the ID whenever it is legal, even if the rest is malformed. */
static u8 rdx_volume_parse(const char *value, rdx_volume_request_t *request)
{
    unsigned int len = 0, first = 0, second, digits, i;
    u32 id = 0;

    if (!value) {
        return 0;
    }
    while (len < CUSTOM_VALUE_MAX_LENGTH && value[len]) {
        ++len;
    }
    while (first < len && value[first] != ',') {
        ++first;
    }
    if (first == len) {
        return 0;
    }
    second = first + 1;
    while (second < len && value[second] != ',') {
        ++second;
    }
    digits = second - first - 1;
    if (!digits || digits > 5) {
        return 0;
    }
    for (i = 0; i < digits; ++i) {
        u8 c = value[first + 1 + i];

        if (c < '0' || c > '9') {
            return 0;
        }
        id = id * 10 + c - '0';
        request->id[i] = c;
    }
    if (!id || id > 65535) {
        return 0;
    }
    request->id[digits] = '\0';
    request->op = RDX_VOLUME_ARG;
    if (len == CUSTOM_VALUE_MAX_LENGTH || first != 3) {
        return 1;
    }
    if (!memcmp(value, "get", 3) && second == len) {
        request->op = RDX_VOLUME_GET;
    } else if (!memcmp(value, "set", 3) && second + 1 < len) {
        unsigned int level_start = second + 1;
        unsigned int level_digits = len - level_start;

        if (level_digits < 1 || level_digits > 2) {
            return 1;
        }
        request->level = 0;
        for (i = 0; i < level_digits; ++i) {
            u8 c = value[level_start + i];

            if (c < '0' || c > '9') {
                return 1;
            }
            request->level = request->level * 10 + c - '0';
        }
        request->op = RDX_VOLUME_SET;
    }
    return 1;
}

static void rdx_volume_reply(const rdx_volume_request_t *request,
                             const char *result, s16 current, u16 max_level)
{
    char packet[48];
    int len;

    if (!result) {
        return;
    }
    if (!strcmp(result, "busy")) {
        current = 0;
        max_level = 0;
    }
    len = snprintf(packet, sizeof(packet), "*DEV#custom#vol#%s,%s,%d,%u#",
                   request->id, result, current, max_level);
    if (len <= 0 || len >= sizeof(packet) ||
        !rdx_volume_token_current(&request->token)) {
        return;
    }
    if (rdx_ble_server_send_text_for_token(packet, len, &request->token)) {
        printf("[VOLUME] reply not accepted\n");
    }
}

static void rdx_volume_execute(rdx_volume_request_t *request)
{
    const char *result = "arg";
    s16 current;
    u16 max_level;

    if (!request) {
        return;
    }
    if (!rdx_volume_token_current(&request->token)) {
        free(request);
        return;
    }
    if (rdx_volume_common_busy() ||
        !rdx_volume_read_range(&current, &max_level)) {
        rdx_volume_reply(request, "busy", 0, 0);
        free(request);
        return;
    }

    if (request->op == RDX_VOLUME_GET) {
        result = "ok";
    } else if (request->op == RDX_VOLUME_SET) {
        if (rdx_volume_set_busy()) {
            result = "busy";
        } else if (request->level > max_level) {
            result = "arg";
        } else {
            s16 level = request->level;

            /* A same-value SET cancels the default state without restarting
             * the SDK's delayed volume-save timer. */
            app_audio_set_volume_def_state(0);
            if (current != level) {
                app_audio_set_volume(APP_AUDIO_STATE_MUSIC, level, 1);
            }
            rdx_volume_cancel_for_token(&request->token);
            current = level;
            result = "ok";
        }
    }
    /* A disconnect race may keep the SET commit device-wide, but the old
     * transport must not receive its ACK or inherit the new owner's token. */
    rdx_volume_reply(request, result, current, max_level);
    free(request);
}

void rdx_volume_handle_custom(const char *value)
{
    rdx_volume_request_t parsed = {0};
    rdx_volume_request_t *request;
    int msg[3];

    if (!rdx_ble_session_rdx_token_capture(&parsed.token, 1) ||
        !rdx_volume_token_current(&parsed.token)) {
        return;
    }
    if (!rdx_volume_parse(value, &parsed)) {
        printf("[VOLUME] invalid request ID\n");
        return;
    }
    request = malloc(sizeof(*request));
    if (!request) {
        return;
    }
    *request = parsed;
    msg[0] = (int)rdx_volume_execute;
    msg[1] = 1;
    msg[2] = (int)request;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        free(request);
        printf("[VOLUME] request queue failed\n");
    }
}

static void rdx_volume_notify_on_app_core(void *priv)
{
    rdx_volume_notify_t *notify = priv;
    char packet[40];
    s16 current;
    u16 max_level;
    int len;

    if (!notify) {
        return;
    }
    if (volume_notify_pending == notify) {
        volume_notify_pending = NULL;
    }
    if (!notify->valid ||
        !rdx_volume_token_current(&notify->token) ||
        !rdx_volume_read_range(&current, &max_level)) {
        free(notify);
        return;
    }
    len = snprintf(packet, sizeof(packet), "*DEV#custom#vol#0,ntf,%d,%u#",
                   current, max_level);
    if (len > 0 && len < sizeof(packet)) {
        if (rdx_ble_server_send_text_for_token(packet, len, &notify->token)) {
            printf("[VOLUME] notification not accepted\n");
        }
    }
    free(notify);
}

static void rdx_volume_notify_timer(void *priv)
{
    rdx_volume_notify_t *notify = priv;
    int msg[3];

    if (!notify) {
        return;
    }
    if (!notify->valid) {
        if (volume_notify_pending == notify) {
            volume_notify_pending = NULL;
        }
        free(notify);
        return;
    }
    msg[0] = (int)rdx_volume_notify_on_app_core;
    msg[1] = 1;
    msg[2] = (int)notify;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        if (volume_notify_pending == notify) {
            volume_notify_pending = NULL;
        }
        free(notify);
    }
}

void rdx_volume_local_music_changed(s16 previous, s16 current)
{
    rdx_volume_notify_t *notify;
    rdx_volume_notify_t *pending;
    rdx_ble_async_token_t token;
    u32 cancel_serial;

    if (previous == current) {
        return;
    }
    local_irq_disable();
    cancel_serial = volume_notify_cancel_serial;
    local_irq_enable();
    if (!rdx_ble_session_rdx_token_capture(&token, 1) ||
        !rdx_volume_token_current(&token)) {
        return;
    }
    local_irq_disable();
    pending = volume_notify_pending;
    if (pending && rdx_volume_token_equal(&pending->token, &token)) {
        local_irq_enable();
        return;
    }
    local_irq_enable();
    notify = malloc(sizeof(*notify));
    if (!notify) {
        return;
    }
    notify->token = token;
    notify->valid = 1;
    local_irq_disable();
    if (cancel_serial != volume_notify_cancel_serial) {
        local_irq_enable();
        free(notify);
        return;
    }
    pending = volume_notify_pending;
    if (pending && rdx_volume_token_equal(&pending->token, &token)) {
        local_irq_enable();
        free(notify);
        return;
    }
    if (pending) {
        pending->valid = 0;
    }
    volume_notify_pending = notify;
    local_irq_enable();
    if (!sys_timeout_add(notify, rdx_volume_notify_timer,
                         RDX_VOLUME_NOTIFY_DELAY_MS)) {
        local_irq_disable();
        if (volume_notify_pending == notify) {
            volume_notify_pending = NULL;
        }
        notify->valid = 0;
        local_irq_enable();
        free(notify);
    }
}

void rdx_volume_cancel_for_token(const rdx_ble_async_token_t *token)
{
    rdx_volume_notify_t *notify;

    if (!token) {
        return;
    }
    local_irq_disable();
    ++volume_notify_cancel_serial;
    notify = volume_notify_pending;
    if (notify && rdx_volume_token_equal(&notify->token, token)) {
        notify->valid = 0;
        volume_notify_pending = NULL;
    }
    local_irq_enable();
}

void rdx_volume_init(void)
{
    volume_notify_pending = NULL;
}

#endif
