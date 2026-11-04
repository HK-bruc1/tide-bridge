#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_flashnote.data.bss")
#pragma data_seg(".rdx_flashnote.data")
#pragma const_seg(".rdx_flashnote.text.const")
#pragma code_seg(".rdx_flashnote.text")
#endif

#include "app_config.h"
#include "rdx_flashnote.h"
#include "rdx_record.h"
#include "rdx_protocol.h"
#include "rdx_ble_server.h"
#include "os/os_api.h"

#if (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define LOG_TAG "[rdx_flashnote]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#define RDX_FLASHNOTE_REQUEST_RETRY_MS 10

enum rdx_flashnote_state {
    RDX_FLASHNOTE_IDLE,
    RDX_FLASHNOTE_STARTING,
    RDX_FLASHNOTE_RUNNING,
    RDX_FLASHNOTE_STOPPING,
};

typedef struct {
    u8 command;
    rdx_ble_async_token_t token;
} rdx_flashnote_request_t;

static struct {
    u8 state;
    u8 owner_valid;
    u8 release_pending;
    u8 start_failed_busy_pending;
    rdx_ble_async_token_t owner;
    u32 request_generation;
    u32 session_generation;
    u32 format;
    u8 start_failure_state;
    u16 retry_timer;
} flashnote;

static struct {
    u8 active;
    u8 local;
    u8 owner_valid;
    u32 session_generation;
    u32 sn;
    char filename[64];
    u32 format;
    rdx_ble_async_token_t owner;
} key_flashnote;

static u8 flashnote_token_equal(const rdx_ble_async_token_t *left,
                                const rdx_ble_async_token_t *right)
{
    return left && right &&
           left->slot_index == right->slot_index &&
           left->slot_generation == right->slot_generation &&
           left->transport_epoch == right->transport_epoch;
}

static void flashnote_owner_clear(void)
{
    flashnote.owner_valid = 0;
    flashnote.owner.slot_index = RDX_BLE_LINK_INVALID_INDEX;
    flashnote.owner.slot_generation = 0;
    flashnote.owner.transport_epoch = 0;
}

static u8 flashnote_owner_is_current(const rdx_ble_async_token_t *token)
{
    return flashnote.owner_valid && flashnote_token_equal(&flashnote.owner, token) &&
           rdx_ble_session_rdx_token_resolve(token, 1);
}

static int flashnote_send_packet(const char *packet, u32 len,
                                 const rdx_ble_async_token_t *token)
{
    if (!packet || !len || !token || !rdx_ble_session_rdx_token_resolve(token, 1)) {
        return -1;
    }
    return rdx_ble_server_send_text_for_token(packet, len, token);
}

static int flashnote_send_busy(const rdx_ble_async_token_t *token)
{
    static const char busy[] = "*DEV#flashnote#2#";
    r_printf("[FLASHNOTE] busy state=%u release=%u\r",
             flashnote.state, flashnote.release_pending);
    return flashnote_send_packet(busy, sizeof(busy) - 1, token);
}

static int flashnote_send_state(u8 state, u32 format,
                                const rdx_ble_async_token_t *token)
{
    char packet[128];
    u32 opus_format = rdx_protocol_calc_opus_format((u8)format);
    int len;
    int ret;

    len = snprintf(packet, sizeof(packet), "*DEV#flashnote#%u#0##%u#",
                   state, (unsigned)opus_format);
    if (len <= 0 || len >= (int)sizeof(packet)) {
        return -1;
    }
    ret = flashnote_send_packet(packet, (u32)len, token);
    r_printf("[FLASHNOTE] TX state=%u format=%u opus=%u len=%u ret=%d token=%u/%u/%u\r",
             state, format, (unsigned)opus_format, (unsigned)len, ret,
             token ? token->slot_index : 0xff,
             token ? (unsigned)token->slot_generation : 0,
             token ? (unsigned)token->transport_epoch : 0);
    return ret;
}

static int flashnote_send_key_state(u8 state, u32 format, u32 sn,
                                    const char *filename,
                                    const rdx_ble_async_token_t *token)
{
    char packet[160];
    u32 opus_format = rdx_protocol_calc_opus_format((u8)format);
    int len;
    int ret;

    len = snprintf(packet, sizeof(packet), "*DEV#flashnote#%u#%u#%s#%u#",
                   state, (unsigned)sn, filename ? filename : "",
                   (unsigned)opus_format);
    if (len <= 0 || len >= (int)sizeof(packet)) {
        return -1;
    }
    ret = flashnote_send_packet(packet, (u32)len, token);
    r_printf("[FLASHNOTE] TX key state=%u sn=%u file=%s format=%u opus=%u len=%u ret=%d token=%u/%u/%u\r",
             state, (unsigned)sn, filename ? filename : "", format,
             (unsigned)opus_format, (unsigned)len, ret,
             token ? token->slot_index : 0xff,
             token ? (unsigned)token->slot_generation : 0,
             token ? (unsigned)token->transport_epoch : 0);
    return ret;
}

static int flashnote_send_start_failure(int err,
                                        const rdx_ble_async_token_t *token)
{
    static const char busy[] = "*DEV#flashnote#2#";
    static const char no_space[] = "*DEV#flashnote#3#";

    if (err == -2) {
        return flashnote_send_packet(no_space, sizeof(no_space) - 1, token);
    }
    return flashnote_send_packet(busy, sizeof(busy) - 1, token);
}

static void flashnote_retry_cancel(void)
{
    if (flashnote.retry_timer) {
        sys_timeout_del(flashnote.retry_timer);
        flashnote.retry_timer = 0;
    }
}

static void flashnote_release_post(void *priv);
static void flashnote_stop_tick(void *priv);
static void flashnote_start_failed_tick(void *priv);

static void flashnote_schedule_release_retry(void)
{
    if (!flashnote.retry_timer) {
        flashnote.retry_timer = sys_timeout_add(NULL, flashnote_release_post,
                                                RDX_FLASHNOTE_REQUEST_RETRY_MS);
    }
}

static void flashnote_release_complete_common(void)
{
    rdx_record_stream_only_release_complete();
    flashnote.release_pending = 0;
    flashnote.start_failed_busy_pending = 0;
    flashnote.state = RDX_FLASHNOTE_IDLE;
    flashnote.session_generation = 0;
    flashnote.format = 0;
    flashnote.start_failure_state = 0;
    flashnote_owner_clear();
    flashnote_retry_cancel();
}

static void flashnote_release_on_app_core(void *priv)
{
    (void)priv;
    if (!flashnote.release_pending) {
        return;
    }
    if (!rdx_record_stream_only_release()) {
        flashnote_schedule_release_retry();
        return;
    }
    flashnote_release_complete_common();
}

static void flashnote_release_post(void *priv)
{
    int msg[2] = {(int)flashnote_release_on_app_core, 0};
    (void)priv;
    flashnote.retry_timer = 0;
    if (flashnote.release_pending &&
        os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)) {
        flashnote_schedule_release_retry();
    }
}

static void flashnote_release_start(u8 keep_owner)
{
    flashnote.release_pending = 1;
    if (!keep_owner) {
        flashnote_owner_clear();
    }
    flashnote_retry_cancel();
    flashnote_release_on_app_core(NULL);
}

static void flashnote_stop_finish(const rdx_ble_async_token_t *owner,
                                  u32 format)
{
    if (flashnote.owner_valid && flashnote_token_equal(&flashnote.owner, owner) &&
        rdx_ble_session_rdx_token_resolve(owner, 1)) {
        (void)flashnote_send_state(RDX_FLASHNOTE_STATE_END, format, owner);
    }
}

static void flashnote_stop_on_app_core(void *priv)
{
    rdx_ble_async_token_t owner;
    u32 format;
    u32 generation = (u32)priv;

    if (!flashnote.release_pending || generation != flashnote.request_generation) {
        return;
    }
    owner = flashnote.owner;
    format = flashnote.format;
    if (!rdx_record_stream_only_release()) {
        flashnote.retry_timer = sys_timeout_add((void *)generation,
                                                flashnote_stop_tick,
                                                RDX_FLASHNOTE_REQUEST_RETRY_MS);
        return;
    }
    flashnote_stop_finish(&owner, format);
    flashnote_release_complete_common();
}

static void flashnote_stop_tick(void *priv)
{
    int msg[3] = {(int)flashnote_stop_on_app_core, 1, (int)priv};
    flashnote.retry_timer = 0;
    if (flashnote.release_pending &&
        os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        flashnote.retry_timer = sys_timeout_add(priv, flashnote_stop_tick,
                                                RDX_FLASHNOTE_REQUEST_RETRY_MS);
        if (!flashnote.retry_timer) {
            r_printf("[FLASHNOTE] STOP retry timer unavailable\n");
        }
    }
}

static void flashnote_start_failed_on_app_core(void *priv)
{
    u32 generation = (u32)priv;
    rdx_ble_async_token_t owner;

    if (generation != flashnote.request_generation ||
        (flashnote.state != RDX_FLASHNOTE_STARTING &&
         flashnote.state != RDX_FLASHNOTE_STOPPING)) {
        return;
    }
    owner = flashnote.owner;
    if (flashnote.owner_valid && rdx_ble_session_rdx_token_resolve(&owner, 1)) {
        (void)flashnote_send_start_failure(flashnote.start_failure_state == 3 ? -2 : -1,
                                           &owner);
    }
    flashnote.state = RDX_FLASHNOTE_STOPPING;
    flashnote.start_failed_busy_pending = 0;
    flashnote_release_start(1);
}

static int flashnote_schedule_start_failed(void)
{
    int msg[3] = {(int)flashnote_start_failed_on_app_core, 1,
                  (int)flashnote.request_generation};
    return os_taskq_post_type("app_core", Q_CALLBACK, 3, msg);
}

static void flashnote_start_failed_tick(void *priv)
{
    (void)priv;
    flashnote.retry_timer = 0;
    if (flashnote.state != RDX_FLASHNOTE_STOPPING ||
        !flashnote.start_failed_busy_pending) {
        return;
    }
    if (flashnote_schedule_start_failed()) {
        flashnote.retry_timer = sys_timeout_add(NULL, flashnote_start_failed_tick,
                                                RDX_FLASHNOTE_REQUEST_RETRY_MS);
        if (!flashnote.retry_timer) {
            r_printf("[FLASHNOTE] init-failure retry timer unavailable\n");
        }
    }
}

static void flashnote_start_failure_begin(u8 state)
{
    flashnote.start_failure_state = state;
    flashnote.state = RDX_FLASHNOTE_STOPPING;
    if (!flashnote_schedule_start_failed()) {
        flashnote.start_failed_busy_pending = 1;
        flashnote.retry_timer = sys_timeout_add(NULL, flashnote_start_failed_tick,
                                                RDX_FLASHNOTE_REQUEST_RETRY_MS);
        if (!flashnote.retry_timer) {
            r_printf("[FLASHNOTE] start-failure retry timer unavailable\n");
        }
    } else {
        /* BUSY is best-effort; an unavailable app_core post must not leave the
         * stream session latched in STOPPING forever. */
        flashnote_release_start(1);
    }
}

static int flashnote_start(const rdx_ble_async_token_t *token,
                           u32 generation)
{
    int ret;
    u32 session_generation = 0;
    u8 format = 0;

    if (flashnote.state == RDX_FLASHNOTE_STARTING) {
        if (flashnote_owner_is_current(token)) {
            return 0; /* Coalesce retransmission while the engine starts. */
        }
        return flashnote_send_busy(token);
    }
    if (flashnote.state == RDX_FLASHNOTE_RUNNING) {
        if (flashnote_owner_is_current(token)) {
            return flashnote_send_state(RDX_FLASHNOTE_STATE_RUNNING,
                                        flashnote.format, token);
        }
        return flashnote_send_busy(token);
    }
    if (flashnote.state == RDX_FLASHNOTE_STOPPING || flashnote.release_pending) {
        return flashnote_send_busy(token);
    }
    if (key_flashnote.active || rdx_record_key_flashnote_session_open()) {
        return flashnote_send_busy(token);
    }

    ret = rdx_record_flashnote_start(token, &session_generation, &format);
    if (ret) {
        return flashnote_send_start_failure(ret, token);
    }
    flashnote.state = RDX_FLASHNOTE_STARTING;
    flashnote.owner = *token;
    flashnote.owner_valid = 1;
    flashnote.session_generation = session_generation;
    flashnote.format = format;
    flashnote.request_generation = generation;
    return 0;
}

static int flashnote_stop(const rdx_ble_async_token_t *token, u32 generation)
{
    if (flashnote.state == RDX_FLASHNOTE_IDLE && !flashnote.release_pending) {
        /* A synchronized key session remains owned by the physical key. */
        if (key_flashnote.active || rdx_record_key_flashnote_session_open()) {
            return flashnote_send_busy(token);
        }
        if (!rdx_ble_session_rdx_token_resolve(token, 1)) {
            return -1;
        }
        return flashnote_send_state(RDX_FLASHNOTE_STATE_END,
                                    rdx_record_get_status()->formate, token);
    }
    if (!flashnote_owner_is_current(token)) {
        return flashnote_send_busy(token);
    }
    if (flashnote.state == RDX_FLASHNOTE_STOPPING || flashnote.release_pending) {
        /* Adopt the repeated request and re-drive the barrier. The prior
         * retry is invalidated by the new request generation. */
        flashnote.request_generation = generation;
        flashnote_retry_cancel();
        if (!rdx_record_stream_only_release()) {
            flashnote.retry_timer = sys_timeout_add((void *)generation,
                                                    flashnote_stop_tick,
                                                    RDX_FLASHNOTE_REQUEST_RETRY_MS);
        } else {
            flashnote_stop_on_app_core((void *)generation);
        }
        return 0;
    }

    flashnote.state = RDX_FLASHNOTE_STOPPING;
    flashnote.request_generation = generation;
    flashnote.release_pending = 1;
    /* The owner remains valid for the final END, but only while this exact
     * RDX token still resolves. A reconnect cannot inherit it. */
    if (!rdx_record_stream_only_release()) {
        flashnote.retry_timer = sys_timeout_add((void *)generation,
                                                flashnote_stop_tick,
                                                RDX_FLASHNOTE_REQUEST_RETRY_MS);
        if (!flashnote.retry_timer) {
            flashnote_release_start(1);
        }
        return 0;
    }
    flashnote_stop_on_app_core((void *)generation);
    return 0;
}

static void flashnote_request_on_app_core(void *priv)
{
    rdx_flashnote_request_t *request = priv;
    u32 generation;

    if (!request) {
        return;
    }
    generation = ++flashnote.request_generation;
    if (!generation) {
        generation = ++flashnote.request_generation;
    }
    if (!rdx_ble_session_rdx_token_resolve(&request->token, 1)) {
        r_printf("[FLASHNOTE] drop stale request command=%u\n", request->command);
        free(request);
        return;
    }
    if (!rdx_record_binding_allowed()) {
        r_printf("[FLASHNOTE] request rejected: unbound\n");
        (void)flashnote_send_busy(&request->token);
    } else if (request->command == RDX_FLASHNOTE_CMD_START) {
        (void)flashnote_start(&request->token, generation);
    } else {
        (void)flashnote_stop(&request->token, generation);
    }
    free(request);
}

int rdx_flashnote_request(u8 command, const rdx_ble_async_token_t *token)
{
    rdx_flashnote_request_t *request;
    int msg[3];

    if (!token || (command != RDX_FLASHNOTE_CMD_START &&
                   command != RDX_FLASHNOTE_CMD_STOP)) {
        return -1;
    }
    request = malloc(sizeof(*request));
    if (!request) {
        return flashnote_send_busy(token);
    }
    request->command = command;
    request->token = *token;
    msg[0] = (int)flashnote_request_on_app_core;
    msg[1] = 1;
    msg[2] = (int)request;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        free(request);
        return flashnote_send_busy(token);
    }
    return 0;
}

u8 rdx_flashnote_protocol_parse(u8 *data, u16 len, u8 *command)
{
    static const u8 prefix[] = "*APP#flashnote#";
    u16 payload_len = len;

    if (!data || !command || (len != 17 && len != 18)) {
        return 0;
    }
    if (len == 18) {
        if (data[17] != '\0') {
            return 0;
        }
        payload_len = 17;
    }
    if (payload_len != 17 || memcmp(data, prefix, sizeof(prefix) - 1) ||
        (data[15] != '0' && data[15] != '1') || data[16] != '#') {
        return 0;
    }
    *command = data[15];
    return 1;
}

int rdx_flashnote_on_stream_started(
    u32 session_generation, u8 format,
    const rdx_ble_async_token_t *token)
{
    if (flashnote.state != RDX_FLASHNOTE_STARTING ||
        flashnote.session_generation != session_generation ||
        !token || !flashnote_owner_is_current(token) ||
        !flashnote_owner_is_current(&flashnote.owner)) {
        return -1;
    }
    if (flashnote_send_state(RDX_FLASHNOTE_STATE_RUNNING, format, token)) {
        r_printf("[FLASHNOTE] RUNNING indication failed\n");
        flashnote_start_failure_begin(RDX_FLASHNOTE_STATE_FAIL_BUSY);
        return -1;
    }
    flashnote.format = format;
    flashnote.state = RDX_FLASHNOTE_RUNNING;
    return 0;
}

int rdx_flashnote_on_key_record_started(
    u32 session_generation, u8 format,
    const rdx_ble_async_token_t *token)
{
    if (!session_generation || !token ||
        !rdx_ble_session_rdx_token_resolve(token, 1)) {
        return -1;
    }
    if (key_flashnote.active) {
        if (key_flashnote.session_generation == session_generation &&
            !key_flashnote.local && key_flashnote.owner_valid &&
            flashnote_token_equal(&key_flashnote.owner, token)) {
            return 0;
        }
        return -1;
    }
    if (flashnote_send_key_state(RDX_FLASHNOTE_STATE_RUNNING, format, 0, "",
                                 token)) {
        return -1;
    }
    key_flashnote.active = 1;
    key_flashnote.local = 0;
    key_flashnote.owner_valid = 1;
    key_flashnote.session_generation = session_generation;
    key_flashnote.sn = 0;
    key_flashnote.filename[0] = '\0';
    key_flashnote.format = format;
    key_flashnote.owner = *token;
    return 0;
}

int rdx_flashnote_on_key_local_record_started(
    u32 session_generation, u8 format, u32 sn, const char *filename)
{
    if (!session_generation || !sn || !filename || !filename[0]) {
        return -1;
    }
    if (key_flashnote.active) {
        if (key_flashnote.local &&
            key_flashnote.session_generation == session_generation &&
            key_flashnote.sn == sn &&
            !strcmp(key_flashnote.filename, filename)) {
            return 0;
        }
        return -1;
    }
    key_flashnote.active = 1;
    key_flashnote.local = 1;
    key_flashnote.owner_valid = 0;
    key_flashnote.session_generation = session_generation;
    key_flashnote.sn = sn;
    snprintf(key_flashnote.filename, sizeof(key_flashnote.filename), "%s",
             filename);
    key_flashnote.format = format;
    return 0;
}

int rdx_flashnote_sync_active(const rdx_ble_async_token_t *token)
{
    RecordStatus *status = rdx_record_get_status();

    if (!key_flashnote.active || !key_flashnote.local || !token ||
        !rdx_ble_session_rdx_token_resolve(token, 1) || !status ||
        (status->run != RECORD_STATE_START &&
         status->run != RECORD_STATE_RESUME)) {
        return -1;
    }
    return flashnote_send_key_state(
        RDX_FLASHNOTE_STATE_RUNNING, key_flashnote.format,
        key_flashnote.sn, key_flashnote.filename, token);
}

void rdx_flashnote_on_key_record_stopped(
    u32 session_generation, u8 format,
    const rdx_ble_async_token_t *token)
{
    u8 owner_current;
    rdx_ble_async_token_t current;

    if (!key_flashnote.active ||
        key_flashnote.session_generation != session_generation) {
        return;
    }
    if (key_flashnote.local && !token) {
        owner_current = rdx_ble_session_rdx_token_capture(&current, 1) &&
                        rdx_ble_session_rdx_token_resolve(&current, 1);
        token = owner_current ? &current : NULL;
    } else {
        owner_current = token && key_flashnote.owner_valid &&
                        flashnote_token_equal(&key_flashnote.owner, token) &&
                        rdx_ble_session_rdx_token_resolve(token, 1);
    }
    if (owner_current) {
        (void)flashnote_send_key_state(
            RDX_FLASHNOTE_STATE_END,
            key_flashnote.format ? key_flashnote.format : format,
            key_flashnote.sn, key_flashnote.filename, token);
    } else {
        r_printf("[FLASHNOTE] drop stale key END generation=%u\r",
                 (unsigned)session_generation);
    }
    key_flashnote.active = 0;
    key_flashnote.local = 0;
    key_flashnote.owner_valid = 0;
    key_flashnote.session_generation = 0;
    key_flashnote.sn = 0;
    key_flashnote.filename[0] = '\0';
    key_flashnote.format = 0;
}

void rdx_flashnote_on_key_local_record_stopped(
    u32 session_generation, u8 format)
{
    if (key_flashnote.active && key_flashnote.local) {
        rdx_flashnote_on_key_record_stopped(session_generation, format, NULL);
    }
}

void rdx_flashnote_on_record_init_failed(int err)
{
    if (flashnote.state != RDX_FLASHNOTE_STARTING) {
        return;
    }
    /* APP flashnote is stream-only; no failure here can mean local no-space. */
    (void)err;
    flashnote_start_failure_begin(RDX_FLASHNOTE_STATE_FAIL_BUSY);
}

void rdx_flashnote_cancel_all(void)
{
    u8 release = flashnote.state != RDX_FLASHNOTE_IDLE ||
                 flashnote.release_pending;

    flashnote_retry_cancel();
    flashnote.state = RDX_FLASHNOTE_IDLE;
    flashnote.session_generation = 0;
    flashnote.format = 0;
    flashnote.start_failure_state = 0;
    flashnote_owner_clear();
    /* An offline F19 session owns a real local file and survives transport
     * loss. Its saved file identity is re-synchronized after reconnect. */
    if (!key_flashnote.local) {
        key_flashnote.active = 0;
        key_flashnote.owner_valid = 0;
        key_flashnote.session_generation = 0;
        key_flashnote.sn = 0;
        key_flashnote.filename[0] = '\0';
        key_flashnote.format = 0;
    }
    if (release) {
        flashnote.release_pending = 1;
        flashnote_release_post(NULL);
    } else {
        flashnote.release_pending = 0;
    }
}

void rdx_flashnote_cancel_for_token(const rdx_ble_async_token_t *token)
{
    if (token && key_flashnote.active &&
        !key_flashnote.local && key_flashnote.owner_valid &&
        flashnote_token_equal(&key_flashnote.owner, token)) {
        key_flashnote.active = 0;
        key_flashnote.owner_valid = 0;
        key_flashnote.session_generation = 0;
        key_flashnote.sn = 0;
        key_flashnote.filename[0] = '\0';
        key_flashnote.format = 0;
    }
    if (!token || !flashnote.owner_valid ||
        !flashnote_token_equal(&flashnote.owner, token)) {
        return;
    }
    rdx_flashnote_cancel_all();
}

#endif
