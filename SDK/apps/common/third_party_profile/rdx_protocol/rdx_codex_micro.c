#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_codex_micro.data.bss")
#pragma data_seg(".rdx_codex_micro.data")
#pragma const_seg(".rdx_codex_micro.text.const")
#pragma code_seg(".rdx_codex_micro.text")
#endif

#include "app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_profile.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_codex_micro.h"
#include "rdx_ble_session.h"
#include "app_ble_spp_api.h"
#include "btstack/le/att.h"
#include "btstack/le/le_user.h"
#include "cJSON.h"

#if TCFG_RDX_CODEX_MICRO_MODE

#define RDX_CODEX_RX_MAX             1024
#define RDX_CODEX_TX_DEPTH           4
#define RDX_CODEX_TX_ITEM_MAX        512
#define RDX_CODEX_METHOD_MAX         48
#define RDX_CODEX_LIGHT_ITEMS_MAX    6
#define RDX_CODEX_JSON_DEPTH_MAX     8
#define RDX_CODEX_ATT_ERR_OFFSET     0x07
#define RDX_CODEX_ATT_ERR_LENGTH     0x0d
#define RDX_CODEX_ATT_ERR_UNLIKELY   0x0e
#define RDX_CODEX_ATT_ERR_VALUE      0x13

typedef struct {
    char data[RDX_CODEX_TX_ITEM_MAX];
    u16 len;
    u16 offset;
    rdx_ble_async_token_t token;
} rdx_codex_tx_item_t;

typedef struct {
    char data[RDX_CODEX_RX_MAX + 1];
    u16 len;
    u16 con_handle;
    s16 depth;
    u8 started;
    u8 in_string;
    u8 escaped;
    u8 complete;
    u8 queued;
    rdx_ble_async_token_t token;
} rdx_codex_rx_state_t;

static rdx_codex_rx_state_t s_codex_rx;
static rdx_codex_tx_item_t s_codex_tx[RDX_CODEX_TX_DEPTH];
static u8 s_codex_tx_head;
static u8 s_codex_tx_count;
static u16 s_codex_tx_bytes;
static u32 s_codex_generation;
static u32 s_codex_rx_drop_count;
static u32 s_codex_tx_drop_count;
static u32 s_codex_buffer_full_count;
static u16 s_codex_agent_release_timer;
static u16 s_codex_rx_timer;

extern u8 rdx_battery_get_percent(void);
extern u8 get_charge_online_flag(void);

static u8 rdx_codex_token_capture(rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_get_hid_link();
    if (!link || !link->connected || !rdx_ble_session_link_is_hid(link)) {
        return 0;
    }
    *token = rdx_ble_session_token_capture(link);
    return token->slot_index != RDX_BLE_LINK_INVALID_INDEX;
}

static rdx_ble_link_state_t *rdx_codex_token_resolve(
    const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_link_token_resolve(token);
    if (!link || !rdx_ble_session_link_is_hid(link)) {
        return NULL;
    }
    return link;
}

static void rdx_codex_rx_clear(void)
{
    if (s_codex_rx_timer) {
        sys_timeout_del(s_codex_rx_timer);
        s_codex_rx_timer = 0;
    }
    memset(&s_codex_rx, 0, sizeof(s_codex_rx));
}

static void rdx_codex_rx_timeout(void *priv)
{
    (void)priv;
    s_codex_rx_timer = 0;
    s_codex_rx_drop_count++;
    s_codex_generation++;
    rdx_codex_rx_clear();
}

static void rdx_codex_tx_clear(void)
{
    memset(s_codex_tx, 0, sizeof(s_codex_tx));
    s_codex_tx_head = 0;
    s_codex_tx_count = 0;
    s_codex_tx_bytes = 0;
}

void rdx_codex_micro_runtime_reset(void)
{
    if (s_codex_agent_release_timer) {
        sys_timeout_del(s_codex_agent_release_timer);
        s_codex_agent_release_timer = 0;
    }
    s_codex_generation++;
    rdx_codex_rx_clear();
    rdx_codex_tx_clear();
}

void rdx_codex_micro_ready_drop_cleanup(void)
{
    if (s_codex_agent_release_timer && rdx_hogp_codex_is_ready()) {
        sys_timeout_del(s_codex_agent_release_timer);
        s_codex_agent_release_timer = 0;
        (void)rdx_codex_micro_send_agent_key(0);
    }
    rdx_codex_micro_runtime_reset();
}

void rdx_codex_micro_init(void)
{
    s_codex_generation = 1;
    s_codex_rx_drop_count = 0;
    s_codex_tx_drop_count = 0;
    s_codex_buffer_full_count = 0;
    rdx_codex_rx_clear();
    rdx_codex_tx_clear();
}

void rdx_codex_micro_deinit(void)
{
    rdx_codex_micro_runtime_reset();
}

static u16 rdx_codex_read_helper(const u8 *data, u16 len, u16 offset,
                                 u8 *buffer, u16 buffer_size)
{
    u16 copy_len;
    if (offset >= len) {
        return 0;
    }
    copy_len = len - offset;
    if (copy_len > buffer_size) {
        copy_len = buffer_size;
    }
    if (buffer && copy_len) {
        memcpy(buffer, data + offset, copy_len);
    }
    return copy_len;
}

u16 rdx_codex_micro_att_read(hci_con_handle_t connection_handle,
                             u16 att_handle, u16 offset,
                             u8 *buffer, u16 buffer_size)
{
    static const u8 empty_report[RDX_CODEX_MICRO_REPORT_BODY_LEN] = {0};
    if (att_handle == HID_CODEX_INPUT_REPORT_VALUE_HANDLE ||
        att_handle == HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE) {
        return rdx_codex_read_helper(empty_report, sizeof(empty_report),
                                     offset, buffer, buffer_size);
    }
    if (att_handle == HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE) {
        u16 cfg = multi_att_get_ccc_config(connection_handle, att_handle);
        u8 value[2] = {(u8)(cfg & 0xff), (u8)(cfg >> 8)};
        return rdx_codex_read_helper(value, sizeof(value), offset,
                                     buffer, buffer_size);
    }
    return 0;
}

static u8 rdx_codex_json_feed(const u8 *data, u8 len)
{
    u8 i;
    for (i = 0; i < len; i++) {
        u8 ch = data[i];
        if (s_codex_rx.complete) {
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                return 0;
            }
            continue;
        }
        if (!s_codex_rx.started) {
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                continue;
            }
            if (ch != '{') {
                return 0;
            }
            s_codex_rx.started = 1;
            s_codex_rx.depth = 1;
        } else if (s_codex_rx.in_string) {
            if (s_codex_rx.escaped) {
                s_codex_rx.escaped = 0;
            } else if (ch == '\\') {
                s_codex_rx.escaped = 1;
            } else if (ch == '"') {
                s_codex_rx.in_string = 0;
            }
        } else if (ch == '"') {
            s_codex_rx.in_string = 1;
        } else if (ch == '{' || ch == '[') {
            s_codex_rx.depth++;
            if (s_codex_rx.depth > RDX_CODEX_JSON_DEPTH_MAX) {
                return 0;
            }
        } else if (ch == '}' || ch == ']') {
            s_codex_rx.depth--;
            if (s_codex_rx.depth < 0) {
                return 0;
            }
            if (s_codex_rx.depth == 0) {
                s_codex_rx.complete = 1;
            }
        }
        if (s_codex_rx.len >= RDX_CODEX_RX_MAX) {
            return 0;
        }
        s_codex_rx.data[s_codex_rx.len++] = (char)ch;
    }
    s_codex_rx.data[s_codex_rx.len] = '\0';
    return 1;
}

static void rdx_codex_process_rx(void *priv);

int rdx_codex_micro_output_write(hci_con_handle_t connection_handle,
                                 u16 offset, const u8 *buffer,
                                 u16 buffer_size)
{
    rdx_ble_async_token_t token;
    rdx_ble_link_state_t *link;
    u8 payload_len;
    int msg[2];
    if (offset != 0) {
        return RDX_CODEX_ATT_ERR_OFFSET;
    }
    if (!buffer || buffer_size != RDX_CODEX_MICRO_REPORT_BODY_LEN) {
        return RDX_CODEX_ATT_ERR_LENGTH;
    }
    if (buffer[0] != 0x02 || buffer[1] > RDX_CODEX_MICRO_REPORT_DATA_LEN) {
        return RDX_CODEX_ATT_ERR_VALUE;
    }
    if (!rdx_hogp_codex_is_ready() || !rdx_codex_token_capture(&token)) {
        return RDX_CODEX_ATT_ERR_UNLIKELY;
    }
    link = rdx_codex_token_resolve(&token);
    if (!link || link->con_handle != connection_handle) {
        return RDX_CODEX_ATT_ERR_UNLIKELY;
    }
    if (s_codex_rx.queued) {
        s_codex_rx_drop_count++;
        return RDX_CODEX_ATT_ERR_UNLIKELY;
    }
    if (!s_codex_rx.started) {
        s_codex_rx.token = token;
        s_codex_rx.con_handle = connection_handle;
    } else if (!rdx_codex_token_resolve(&s_codex_rx.token) ||
               s_codex_rx.con_handle != connection_handle) {
        rdx_codex_rx_clear();
        return RDX_CODEX_ATT_ERR_UNLIKELY;
    }
    payload_len = buffer[1];
    if (!rdx_codex_json_feed(buffer + 2, payload_len)) {
        s_codex_rx_drop_count++;
        rdx_codex_rx_clear();
        return RDX_CODEX_ATT_ERR_VALUE;
    }
    if (!s_codex_rx.complete) {
        if (!s_codex_rx_timer) {
            s_codex_rx_timer = sys_timeout_add(
                NULL, rdx_codex_rx_timeout, 2000);
            if (!s_codex_rx_timer) {
                rdx_codex_rx_clear();
                return RDX_CODEX_ATT_ERR_UNLIKELY;
            }
        }
        return 0;
    }
    if (s_codex_rx_timer) {
        sys_timeout_del(s_codex_rx_timer);
        s_codex_rx_timer = 0;
    }
    s_codex_rx.queued = 1;
    msg[0] = (int)rdx_codex_process_rx;
    msg[1] = 0;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)) {
        s_codex_rx_drop_count++;
        rdx_codex_rx_clear();
        return RDX_CODEX_ATT_ERR_UNLIKELY;
    }
    return 0;
}

static u8 rdx_codex_id_copy(cJSON *response, const cJSON *id)
{
    cJSON *copy = cJSON_Duplicate(id, 1);
    return copy && cJSON_AddItemToObject(response, "id", copy);
}

static u8 rdx_codex_light_valid(const cJSON *item, u8 require_id)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(item, "c");
    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(item, "b");
    const cJSON *effect = cJSON_GetObjectItemCaseSensitive(item, "e");
    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(item, "s");
    if (!cJSON_IsObject(item) ||
        (require_id && (!cJSON_IsNumber(id) || id->valuedouble != id->valueint)) ||
        !cJSON_IsNumber(color) || color->valuedouble < 0 ||
        color->valuedouble > 16777215 ||
        color->valuedouble != color->valueint || !cJSON_IsNumber(brightness) ||
        !cJSON_IsString(effect) || !effect->valuestring ||
        strlen(effect->valuestring) > 16 || !cJSON_IsNumber(speed)) {
        return 0;
    }
    return 1;
}

static u8 rdx_codex_params_valid(const char *method, const cJSON *params)
{
    if (!strcmp(method, "v.oai.thstatus")) {
        int count;
        int i;
        if (!cJSON_IsArray(params)) {
            return 0;
        }
        count = cJSON_GetArraySize(params);
        if (count > RDX_CODEX_LIGHT_ITEMS_MAX) {
            return 0;
        }
        for (i = 0; i < count; i++) {
            if (!rdx_codex_light_valid(cJSON_GetArrayItem(params, i), 1)) {
                return 0;
            }
        }
        return 1;
    }
    if (!strcmp(method, "v.oai.rgbcfg")) {
        const cJSON *ambient;
        const cJSON *keys;
        if (!cJSON_IsObject(params)) {
            return 0;
        }
        ambient = cJSON_GetObjectItemCaseSensitive(params, "ambient");
        keys = cJSON_GetObjectItemCaseSensitive(params, "keys");
        return rdx_codex_light_valid(ambient, 0) &&
               rdx_codex_light_valid(keys, 0);
    }
    return 1;
}

static void rdx_codex_tx_pump(void *priv)
{
    (void)priv;
    while (s_codex_tx_count) {
        rdx_codex_tx_item_t *item = &s_codex_tx[s_codex_tx_head];
        rdx_ble_link_state_t *link = rdx_codex_token_resolve(&item->token);
        u8 report[RDX_CODEX_MICRO_REPORT_BODY_LEN] = {0};
        u16 remaining;
        u8 chunk;
        int ret;
        if (!link || !rdx_hogp_codex_is_ready()) {
            rdx_codex_micro_runtime_reset();
            return;
        }
        remaining = item->len - item->offset;
        chunk = remaining > RDX_CODEX_MICRO_REPORT_DATA_LEN ?
                RDX_CODEX_MICRO_REPORT_DATA_LEN : (u8)remaining;
        report[0] = 0x02;
        report[1] = chunk;
        memcpy(report + 2, item->data + item->offset, chunk);
        ret = app_ble_att_send_data(link->ble_hdl,
                                    HID_CODEX_INPUT_REPORT_VALUE_HANDLE,
                                    report, sizeof(report), ATT_OP_NOTIFY);
        if (ret == APP_BLE_BUFF_FULL) {
            s_codex_buffer_full_count++;
            att_server_request_can_send_now_event(link->con_handle);
            return;
        }
        if (ret != APP_BLE_NO_ERROR) {
            s_codex_tx_drop_count++;
            rdx_codex_micro_runtime_reset();
            return;
        }
        item->offset += chunk;
        if (item->offset == item->len) {
            s_codex_tx_bytes -= item->len;
            memset(item, 0, sizeof(*item));
            s_codex_tx_head = (s_codex_tx_head + 1) % RDX_CODEX_TX_DEPTH;
            s_codex_tx_count--;
        }
    }
}

static int rdx_codex_tx_enqueue(const char *json)
{
    u16 len;
    u8 tail;
    rdx_codex_tx_item_t *item;
    if (!json || !rdx_hogp_codex_is_ready()) {
        return -1;
    }
    len = (u16)strlen(json);
    if (len + 1 >= RDX_CODEX_TX_ITEM_MAX ||
        s_codex_tx_count >= RDX_CODEX_TX_DEPTH ||
        s_codex_tx_bytes + len + 1 > 2048) {
        s_codex_tx_drop_count++;
        return -1;
    }
    tail = (s_codex_tx_head + s_codex_tx_count) % RDX_CODEX_TX_DEPTH;
    item = &s_codex_tx[tail];
    memset(item, 0, sizeof(*item));
    if (!rdx_codex_token_capture(&item->token)) {
        return -1;
    }
    memcpy(item->data, json, len);
    item->data[len++] = '\n';
    item->len = len;
    s_codex_tx_count++;
    s_codex_tx_bytes += len;
    rdx_codex_tx_pump(NULL);
    return 0;
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
        rdx_codex_tx_enqueue(json);
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

static void rdx_codex_process_rx(void *priv)
{
    cJSON *request;
    rdx_ble_async_token_t token = s_codex_rx.token;
    u32 generation = s_codex_generation;
    (void)priv;
    if (!s_codex_rx.queued || !rdx_codex_token_resolve(&token)) {
        rdx_codex_rx_clear();
        return;
    }
    request = cJSON_ParseWithLength(s_codex_rx.data, s_codex_rx.len);
    rdx_codex_rx_clear();
    if (!request || generation != s_codex_generation ||
        !rdx_codex_token_resolve(&token)) {
        cJSON_Delete(request);
        return;
    }
    rdx_codex_dispatch(request);
    cJSON_Delete(request);
}

void rdx_codex_micro_on_can_send_now(void)
{
    int msg[2];
    if (!s_codex_tx_count) {
        return;
    }
    msg[0] = (int)rdx_codex_tx_pump;
    msg[1] = 0;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)) {
        s_codex_tx_drop_count++;
        rdx_codex_micro_runtime_reset();
    }
}

int rdx_codex_micro_send_agent_key(u8 action)
{
    char json[96];
    if (action > 1) {
        return -1;
    }
    snprintf(json, sizeof(json),
             "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"AG00\",\"act\":%u,\"ag\":0}}",
             action);
    return rdx_codex_tx_enqueue(json);
}

static void rdx_codex_agent_release(void *priv)
{
    (void)priv;
    s_codex_agent_release_timer = 0;
    rdx_codex_micro_send_agent_key(0);
}

int rdx_codex_micro_agent_key_click(void)
{
    if (s_codex_agent_release_timer || rdx_codex_micro_send_agent_key(1)) {
        return -1;
    }
    s_codex_agent_release_timer = sys_timeout_add(
        NULL, rdx_codex_agent_release, TCFG_RDX_HOGP_KEY_UP_DELAY_MS);
    if (!s_codex_agent_release_timer) {
        rdx_codex_micro_send_agent_key(0);
        return -1;
    }
    return 0;
}

#else

void rdx_codex_micro_init(void) {}
void rdx_codex_micro_deinit(void) {}
void rdx_codex_micro_runtime_reset(void) {}
void rdx_codex_micro_ready_drop_cleanup(void) {}
u16 rdx_codex_micro_att_read(hci_con_handle_t c, u16 h, u16 o, u8 *b, u16 s)
{ (void)c; (void)h; (void)o; (void)b; (void)s; return 0; }
int rdx_codex_micro_output_write(hci_con_handle_t c, u16 o, const u8 *b, u16 s)
{ (void)c; (void)o; (void)b; (void)s; return -1; }
void rdx_codex_micro_on_can_send_now(void) {}
int rdx_codex_micro_send_agent_key(u8 action) { (void)action; return -1; }
int rdx_codex_micro_agent_key_click(void) { return -1; }

#endif
