#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_codex_micro.data.bss")
#pragma data_seg(".rdx_codex_micro.data")
#pragma const_seg(".rdx_codex_micro.text.const")
#pragma code_seg(".rdx_codex_micro.text")
#endif

#include "app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hid_service.h"
#include "rdx_codex_micro.h"
#include "rdx_codex_transport.h"
#include "cJSON.h"

#if TCFG_RDX_CODEX_MICRO_MODE

#define RDX_CODEX_METHOD_MAX         48

static u16 s_codex_fast_release_timer;

extern u8 rdx_battery_get_percent(void);
extern u8 get_charge_online_flag(void);

void rdx_codex_micro_init(void)
{
    s_codex_fast_release_timer = 0;
}

void rdx_codex_micro_deinit(void)
{
    if (s_codex_fast_release_timer) {
        sys_timeout_del(s_codex_fast_release_timer);
        s_codex_fast_release_timer = 0;
    }
}

void rdx_codex_micro_ready_drop_cleanup(void)
{
    rdx_codex_micro_fast_key_release_all();
    rdx_codex_transport_runtime_reset();
}

static u8 rdx_codex_id_copy(cJSON *response, const cJSON *id)
{
    cJSON *copy = cJSON_Duplicate(id, 1);
    return copy && cJSON_AddItemToObject(response, "id", copy);
}

static u8 rdx_codex_params_valid(const char *method, const cJSON *params)
{
    /* Lighting fields are optional and may evolve; match the reference firmware's top-level checks. */
    if (!strcmp(method, "v.oai.thstatus")) {
        return cJSON_IsArray(params);
    }
    if (!strcmp(method, "v.oai.rgbcfg")) {
        return cJSON_IsObject(params);
    }
    return 1;
}

static void rdx_codex_send_response(const cJSON *id, cJSON *result,
                                    int error_code, const char *error_message)
{
    cJSON *response;
    char *json;
    if (!id) {
        cJSON_Delete(result);
        return;
    }
    response = cJSON_CreateObject();
    if (!response || !rdx_codex_id_copy(response, id)) {
        cJSON_Delete(response);
        cJSON_Delete(result);
        return;
    }
    if (error_message) {
        cJSON *error = cJSON_AddObjectToObject(response, "error");
        if (error) {
            cJSON_AddNumberToObject(error, "code", error_code);
            cJSON_AddStringToObject(error, "message", error_message);
        }
        cJSON_Delete(result);
    } else {
        cJSON_AddItemToObject(response, "result", result);
    }
    json = cJSON_PrintUnformatted(response);
    if (json) {
        rdx_codex_transport_send_json(json);
        cJSON_free(json);
    }
    cJSON_Delete(response);
}

static cJSON *rdx_codex_ok_result(void)
{
    cJSON *result = cJSON_CreateObject();
    if (result) {
        cJSON_AddTrueToObject(result, "ok");
    }
    return result;
}

static void rdx_codex_dispatch(cJSON *request)
{
    const cJSON *method_item = cJSON_GetObjectItemCaseSensitive(request, "method");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(request, "id");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(request, "params");
    const char *method;
    cJSON *result;
    if (!cJSON_IsObject(request) || !cJSON_IsString(method_item) ||
        !method_item->valuestring ||
        strlen(method_item->valuestring) > RDX_CODEX_METHOD_MAX) {
        rdx_codex_send_response(id, NULL, -32600, "Invalid Request");
        return;
    }
    method = method_item->valuestring;
    if (!strcmp(method, "device.status")) {
        u8 battery = rdx_battery_get_percent();
        result = cJSON_CreateObject();
        if (battery > 100) {
            battery = 100;
        }
        cJSON_AddStringToObject(result, "version", "t2620-mvp");
        cJSON_AddNumberToObject(result, "profile_index", 0);
        cJSON_AddNumberToObject(result, "layer_index", 1);
        cJSON_AddNumberToObject(result, "battery", battery);
        cJSON_AddBoolToObject(result, "is_charging",
                              get_charge_online_flag() ? 1 : 0);
        rdx_codex_send_response(id, result, 0, NULL);
        return;
    }
    if (!strcmp(method, "sys.version")) {
        result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "version", "t2620-mvp");
        rdx_codex_send_response(id, result, 0, NULL);
        return;
    }
    if (!strcmp(method, "v.oai.thstatus") ||
        !strcmp(method, "v.oai.rgbcfg")) {
        if (!rdx_codex_params_valid(method, params)) {
            rdx_codex_send_response(id, NULL, -32602, "Invalid params");
        } else {
            rdx_codex_send_response(id, rdx_codex_ok_result(), 0, NULL);
        }
        return;
    }
    if (!strcmp(method, "lights.preview") ||
        !strcmp(method, "host.focused_app")) {
        rdx_codex_send_response(id, rdx_codex_ok_result(), 0, NULL);
        return;
    }
    rdx_codex_send_response(id, NULL, -32601, "Method not found");
}

void rdx_codex_micro_process_json(const char *json, u16 len)
{
    cJSON *request = cJSON_ParseWithLength(json, len);
    if (!request) {
        return;
    }
    rdx_codex_dispatch(request);
    cJSON_Delete(request);
}

static int rdx_codex_micro_send_fast_key(u8 action)
{
    char json[96];
    if (action > 1) {
        return -1;
    }
    snprintf(json, sizeof(json),
             "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"ACT06\",\"act\":%u}}",
             action);
    return rdx_codex_transport_send_json(json);
}

static void rdx_codex_fast_release(void *priv)
{
    (void)priv;
    s_codex_fast_release_timer = 0;
    rdx_codex_micro_send_fast_key(0);
}

int rdx_codex_micro_fast_key_click(void)
{
    if (s_codex_fast_release_timer || rdx_codex_micro_send_fast_key(1)) {
        return -1;
    }
    s_codex_fast_release_timer = sys_timeout_add(
        NULL, rdx_codex_fast_release, TCFG_RDX_HOGP_KEY_UP_DELAY_MS);
    if (!s_codex_fast_release_timer) {
        rdx_codex_micro_send_fast_key(0);
        return -1;
    }
    return 0;
}

void rdx_codex_micro_fast_key_release_all(void)
{
    if (!s_codex_fast_release_timer) {
        return;
    }
    sys_timeout_del(s_codex_fast_release_timer);
    s_codex_fast_release_timer = 0;
    if (rdx_hid_report_is_ready(RDX_HID_REPORT_CODEX)) {
        (void)rdx_codex_micro_send_fast_key(0);
    }
}

#else

void rdx_codex_micro_init(void) {}
void rdx_codex_micro_deinit(void) {}
void rdx_codex_micro_ready_drop_cleanup(void) {}
void rdx_codex_micro_process_json(const char *j, u16 l)
{ (void)j; (void)l; }
int rdx_codex_micro_fast_key_click(void) { return -1; }
void rdx_codex_micro_fast_key_release_all(void) {}

#endif
