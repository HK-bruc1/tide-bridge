#include "app_config.h"
#include "system/includes.h"
#include "app_main.h"
#include "rdx_app_config.h"
#include "rdx_ble_server.h"
#include "rdx_ble_session.h"
#include "rdx_dut.h"
#include "rdx_factory_audio.h"
#include "rdx_record_tone.h"
#include "rdx_protocol.h"
#include "rdx_record.h"
#include "rdx_storage_lifecycle.h"
#include "rdx_uxfile.h"
#include "syscfg_id.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

enum { TONE_ARG, TONE_GET, TONE_SET };
typedef struct {
    rdx_ble_async_token_t token;
    char id[6];
    u8 op;
    u8 enable;
} rdx_record_tone_request_t;

static u8 tone_enabled = 1;
static u8 tone_durable;
static u8 tone_resetting;

extern u8 get_ota_status(void);
extern bool rdx_app_get_poweroff_flag(void);

void rdx_record_tone_init(void)
{
    u8 saved = 0xff;

    tone_enabled = 1;
    tone_resetting = 0;
    tone_durable = syscfg_read(VM_RDX_RECORD_TONE, &saved, sizeof(saved)) ==
                   sizeof(saved) && saved <= 1;
    if (tone_durable) {
        tone_enabled = saved;
    } else {
        printf("[RECTONE] invalid/missing VM; default ON\n");
    }
}

u8 rdx_record_tone_enabled(void)
{
    return tone_enabled;
}

static int rdx_record_tone_save(u8 enable)
{
    u8 verified = 0xff;

    if (tone_durable && tone_enabled == enable) {
        return 0;
    }
    /* A failed write/readback may already have changed Flash. Do not permit
     * a later same-value SET to confirm persistence using the old RAM value. */
    tone_durable = 0;
    if (syscfg_write(VM_RDX_RECORD_TONE, &enable, sizeof(enable)) != sizeof(enable) ||
        syscfg_read(VM_RDX_RECORD_TONE, &verified, sizeof(verified)) != sizeof(verified) ||
        verified != enable) {
        printf("[RECTONE] VM write/readback failed\n");
        return -1;
    }
    tone_enabled = enable;
    tone_durable = 1;
    return 0;
}

static u8 rdx_record_tone_busy(void)
{
    return tone_resetting || app_var.goto_poweroff_flag || rdx_app_get_poweroff_flag() ||
           get_ota_status() || rdx_dut_is_in_mode() || rdx_dut_test_keys_active() ||
           rdx_factory_audio_owned() || rdx_factory_audio_file_busy() ||
           rdx_storage_lifecycle_business_blocked() ||
           rdx_uxfile_is_formatting() || rdx_record_tone_config_busy();
}

int rdx_record_tone_factory_reset(void)
{
    /* The unified reset entry owns admission, including factory packaging. */
    if (rdx_record_tone_save(1)) {
        return -1;
    }
    tone_resetting = 1;
    return 0;
}

static u8 rdx_record_tone_token_current(const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_rdx_token_resolve(token, 1);
    return link && link->rdx_ccc_configured;
}

/* Recover a legal second-field ID even when the remaining grammar is invalid.
 * The library supplies a NUL-terminated value; never scan beyond its bound. */
static u8 rdx_record_tone_parse(const char *value,
                              rdx_record_tone_request_t *request)
{
    unsigned int len = 0, first = 0, end, digits, i;
    u32 id = 0;

    if (!value) return 0;
    while (len < CUSTOM_VALUE_MAX_LENGTH && value[len]) ++len;
    while (first < len && value[first] != ',') ++first;
    if (first == len) return 0;
    end = first + 1;
    while (end < len && value[end] != ',') ++end;
    digits = end - first - 1;
    if (!digits || digits > 5) return 0;
    for (i = 0; i < digits; ++i) {
        u8 c = value[first + 1 + i];
        if (c < '0' || c > '9') return 0;
        id = id * 10 + c - '0';
        request->id[i] = c;
    }
    if (!id || id > 65535) return 0;
    request->id[digits] = '\0';
    request->op = TONE_ARG;
    if (len == CUSTOM_VALUE_MAX_LENGTH || first != 3) return 1;
    if (!memcmp(value, "get", 3) && end == len) {
        request->op = TONE_GET;
    } else if (!memcmp(value, "set", 3) && end + 2 == len &&
               (value[end + 1] == '0' || value[end + 1] == '1')) {
        request->op = TONE_SET;
        request->enable = value[end + 1] - '0';
    }
    return 1;
}

static void rdx_record_tone_reply(const rdx_record_tone_request_t *request,
                                 const char *result)
{
    char packet[35];
    int len = snprintf(packet, sizeof(packet), "*DEV#custom#rectone#%s,%s,%u#",
                       request->id, result, tone_enabled);

    if (len <= 0 || len >= sizeof(packet) ||
        !rdx_record_tone_token_current(&request->token)) return;
    if (rdx_ble_server_send_text_for_token(packet, len, &request->token)) {
        printf("[RECTONE] reply not accepted\n");
    }
}

static void rdx_record_tone_execute(rdx_record_tone_request_t *request)
{
    const char *result = "arg";

    if (!rdx_record_tone_token_current(&request->token)) {
        free(request);
        return;
    }
    if (request->op == TONE_GET) {
        result = "ok";
    } else if (request->op == TONE_SET) {
        /* START admission and this complete idle/check/commit/publication run
         * on app_core. No deferred SET, audio lock or new service timer. */
        if (rdx_record_tone_busy()) {
            result = "busy";
        } else if (!rdx_record_tone_token_current(&request->token)) {
            free(request);
            return;
        } else {
            result = rdx_record_tone_save(request->enable) ? "vmerr" : "ok";
        }
    }
    /* Commit remains device-wide if disconnect races with Flash IO; only the
     * ACK is suppressed. Never recapture a new owner's token here. */
    rdx_record_tone_reply(request, result);
    free(request);
}

void rdx_record_tone_handle_custom(const char *value)
{
    rdx_record_tone_request_t parsed = {0};
    rdx_record_tone_request_t *request;
    int msg[3];

    if (!rdx_ble_session_rdx_token_capture(&parsed.token, 1) ||
        !rdx_record_tone_token_current(&parsed.token)) return;
    if (!rdx_record_tone_parse(value, &parsed)) {
        printf("[RECTONE] invalid request ID\n");
        return;
    }
    request = malloc(sizeof(*request));
    if (!request) return;
    *request = parsed;
    msg[0] = (int)rdx_record_tone_execute;
    msg[1] = 1;
    msg[2] = (int)request;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        free(request);
        printf("[RECTONE] request queue failed\n");
    }
}

#endif
