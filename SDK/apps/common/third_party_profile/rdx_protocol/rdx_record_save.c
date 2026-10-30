#include "app_config.h"
#include "system/includes.h"
#include "rdx_app_config.h"
#include "rdx_ble_server.h"
#include "rdx_ble_session.h"
#include "rdx_record_save.h"
#include "rdx_record_tone.h"
#include "rdx_protocol.h"
#include "syscfg_id.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

/* Match rectone's VM commit and token-bound custom command contract. */
enum { SAVE_ARG, SAVE_GET, SAVE_SET };
typedef struct {
    rdx_ble_async_token_t token;
    char id[6];
    u8 op;
    u8 enable;
} rdx_record_save_request_t;

static u8 save_enabled = 1;
static u8 save_durable;

void rdx_record_save_init(void)
{
    u8 saved = 0xff;

    save_enabled = 1;
    save_durable = syscfg_read(VM_RDX_RECORD_SAVE, &saved, sizeof(saved)) ==
                   sizeof(saved) && saved <= 1;
    if (save_durable) {
        save_enabled = saved;
    } else {
        printf("[RECSAVE] invalid/missing VM; default ON\n");
    }
}

u8 rdx_record_save_enabled(void)
{
    return save_enabled;
}

static int rdx_record_save_store(u8 enable)
{
    u8 verified = 0xff;

    if (save_durable && save_enabled == enable) {
        return 0;
    }
    /* Failed IO may have changed Flash; the next same-value SET must verify it. */
    save_durable = 0;
    if (syscfg_write(VM_RDX_RECORD_SAVE, &enable, sizeof(enable)) != sizeof(enable) ||
        syscfg_read(VM_RDX_RECORD_SAVE, &verified, sizeof(verified)) != sizeof(verified) ||
        verified != enable) {
        printf("[RECSAVE] VM write/readback failed\n");
        return -1;
    }
    save_enabled = enable;
    save_durable = 1;
    return 0;
}

int rdx_record_save_factory_reset(void)
{
    /* The unified reset entry owns admission. */
    return rdx_record_save_store(1);
}

static u8 rdx_record_save_token_current(const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_rdx_token_resolve(token, 1);
    return link && link->rdx_ccc_configured;
}

/* Recover a legal ID for an arg reply even if the remaining grammar is invalid. */
static u8 rdx_record_save_parse(const char *value,
                                rdx_record_save_request_t *request)
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
    request->op = SAVE_ARG;
    if (len == CUSTOM_VALUE_MAX_LENGTH || first != 3) return 1;
    if (!memcmp(value, "get", 3) && end == len) {
        request->op = SAVE_GET;
    } else if (!memcmp(value, "set", 3) && end + 2 == len &&
               (value[end + 1] == '0' || value[end + 1] == '1')) {
        request->op = SAVE_SET;
        request->enable = value[end + 1] - '0';
    }
    return 1;
}

static void rdx_record_save_reply(const rdx_record_save_request_t *request,
                                  const char *result)
{
    char packet[35];
    int len = snprintf(packet, sizeof(packet), "*DEV#custom#recsave#%s,%s,%u#",
                       request->id, result, save_enabled);

    if (len <= 0 || len >= sizeof(packet) ||
        !rdx_record_save_token_current(&request->token)) return;
    if (rdx_ble_server_send_text_for_token(packet, len, &request->token)) {
        printf("[RECSAVE] reply not accepted\n");
    }
}

static void rdx_record_save_execute(rdx_record_save_request_t *request)
{
    const char *result = "arg";

    if (!rdx_record_save_token_current(&request->token)) {
        free(request);
        return;
    }
    if (request->op == SAVE_GET) {
        result = "ok";
    } else if (request->op == SAVE_SET) {
        /* START admission and this complete check/commit run on app_core. */
        if (rdx_record_config_busy()) {
            result = "busy";
        } else if (!rdx_record_save_token_current(&request->token)) {
            free(request);
            return;
        } else {
            result = rdx_record_save_store(request->enable) ? "vmerr" : "ok";
        }
    }
    rdx_record_save_reply(request, result);
    free(request);
}

void rdx_record_save_handle_custom(const char *value)
{
    rdx_record_save_request_t parsed = {0};
    rdx_record_save_request_t *request;
    int msg[3];

    if (!rdx_ble_session_rdx_token_capture(&parsed.token, 1) ||
        !rdx_record_save_token_current(&parsed.token)) return;
    if (!rdx_record_save_parse(value, &parsed)) {
        printf("[RECSAVE] invalid request ID\n");
        return;
    }
    request = malloc(sizeof(*request));
    if (!request) return;
    *request = parsed;
    msg[0] = (int)rdx_record_save_execute;
    msg[1] = 1;
    msg[2] = (int)request;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        free(request);
        printf("[RECSAVE] request queue failed\n");
    }
}

#endif
