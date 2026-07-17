/* A1 keymap service: authorization, revisioning, idempotency and executor publish. */

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_keymap_config.data.bss")
#pragma data_seg(".rdx_hogp_keymap_config.data")
#pragma const_seg(".rdx_hogp_keymap_config.text.const")
#pragma code_seg(".rdx_hogp_keymap_config.text")
#endif

#include "app_config.h"
#include "system/includes.h"

#include "rdx_app_config.h"
#include "rdx_ble_server.h"
#include "rdx_ble_session.h"
#include "rdx_ble_mode_controller.h"
#include "rdx_dut.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_key_action.h"
#include "rdx_hogp_keymap_config.h"
#include "rdx_hogp_keymap_internal.h"
#include "rdx_protocol.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

typedef struct {
    u8 valid;
    u8 opcode;
    u16 request_id;
    u32 request_frame_crc32;
    u32 revision;
    u32 keymap_crc32;
} rdx_hogpkm_cache_t;

/* A1 leaves the factory keymap TBD, so production defaults to disabled keys. */
static const u8 s_rdx_hogpkm_default_keymap[RDX_HOGPKM_KEYMAP_LEN] = {0};

static u8 s_rdx_hogpkm_current_keymap[RDX_HOGPKM_KEYMAP_LEN];
static u32 s_rdx_hogpkm_current_revision;
static u32 s_rdx_hogpkm_current_keymap_crc32;
static u8 s_rdx_hogpkm_active_slot = RDX_HOGPKM_VM_SLOT_NONE;
static volatile u32 s_rdx_hogpkm_generation;
static rdx_hogpkm_request_t s_rdx_hogpkm_pending;
static rdx_hogpkm_cache_t s_rdx_hogpkm_cache;

extern u8 get_ota_status(void);
extern bool rdx_app_get_poweroff_flag(void);

__attribute__((weak)) int rdx_hogp_keymap_product_authorized(void)
{
    /* VM bound state records ownership, not the current BLE peer identity. */
    return 1;
}

#define RDX_HOGPKM_UPLINK_LEN \
    ((sizeof(CMD_UP_CUSTOM) - 1) + \
     (sizeof(RDX_HOGP_KEYMAP_CUSTOM_CMD) - 1) + 1 + \
     RDX_HOGPKM_MAX_RESPONSE_HEX_LEN + 1)

typedef char rdx_hogpkm_uplink_len_check[
    (RDX_HOGPKM_UPLINK_LEN == 120) ? 1 : -1
];

#if RDX_HOGPKM_TRACE_ENABLE
#define HOGPKM_TRACE(...) y_printf(__VA_ARGS__)
static void rdx_hogpkm_log_keymap(const char *tag, const u8 *payload)
{
    u8 key;

    for (key = 0; key < RDX_HOGPKM_KEY_COUNT; key++) {
        const u8 *entry = &payload[key * RDX_HOGPKM_ENTRY_LEN];
        HOGPKM_TRACE("[HOGPKM] %s key%u mod=%02X usages=%02X,%02X,%02X,%02X,%02X,%02X\n",
                 tag, key + 1, entry[0],
                 entry[1], entry[2], entry[3], entry[4], entry[5], entry[6]);
    }
}
#else
#define HOGPKM_TRACE(...)
#define rdx_hogpkm_log_keymap(tag, payload) do { } while (0)
#endif

static int rdx_hogpkm_send_custom_value(const char *value)
{
    /* librdxApp.a was built with the legacy 100-byte custom value buffer.
     * A 100-character GET response plus NUL overwrites that wrapper's return
     * address, so this formal path builds the complete packet explicitly. */
    static u8 packet[RDX_HOGPKM_UPLINK_LEN + 1];
    int sent_len;
    u16 value_len = (u16)strlen(value);
    u16 offset = 0;

    if (value_len > RDX_HOGPKM_MAX_RESPONSE_HEX_LEN) {
        return -1;
    }

    memcpy(&packet[offset], CMD_UP_CUSTOM, sizeof(CMD_UP_CUSTOM) - 1);
    offset += sizeof(CMD_UP_CUSTOM) - 1;
    memcpy(&packet[offset],
           RDX_HOGP_KEYMAP_CUSTOM_CMD,
           sizeof(RDX_HOGP_KEYMAP_CUSTOM_CMD) - 1);
    offset += sizeof(RDX_HOGP_KEYMAP_CUSTOM_CMD) - 1;
    packet[offset++] = '#';
    memcpy(&packet[offset], value, value_len);
    offset += value_len;
    packet[offset++] = '#';
    packet[offset] = '\0';

    HOGPKM_TRACE("[HOGPKM] uplink len=%u value_len=%u\n", offset, value_len);
    sent_len = rdx_protocol_packet_send_priority(packet, offset);
    return (sent_len > 0) ? 0 : -1;
}

static void rdx_hogpkm_send_response(u8 request_opcode,
                                     u16 request_id,
                                     u32 revision,
                                     const u8 *payload,
                                     u16 payload_len,
                                     u32 generation)
{
    /* app_core has a small stack; A1 GET responses need 101 bytes including
     * NUL, so keep the serialized response workspace in module BSS. */
    static char hex[RDX_HOGPKM_MAX_RESPONSE_HEX_LEN + 1];

    if (generation != s_rdx_hogpkm_generation) {
        HOGPKM_TRACE("[HOGPKM] drop stale response gen=%u curr=%u\n",
                     generation, s_rdx_hogpkm_generation);
        return;
    }
    if (rdx_hogpkm_encode_response(request_opcode,
                                   request_id,
                                   revision,
                                   payload,
                                   payload_len,
                                   hex,
                                   sizeof(hex)) < 0) {
        HOGPKM_TRACE("[HOGPKM] response encode failed: %d\n", payload_len);
        return;
    }
    HOGPKM_TRACE("[HOGPKM] rsp op=%02X rid=%u rev=%u payload_len=%u\n",
             request_opcode | 0x80, request_id, revision, payload_len);
    if (generation != s_rdx_hogpkm_generation) {
        HOGPKM_TRACE("[HOGPKM] drop stale encoded response\n");
        return;
    }
    if (rdx_hogpkm_send_custom_value(hex)) {
        HOGPKM_TRACE("[HOGPKM] uplink queue failed\n");
    }
}

static void rdx_hogpkm_send_status(u8 request_opcode,
                                   u16 request_id,
                                   u8 status,
                                   u32 generation)
{
    HOGPKM_TRACE("[HOGPKM] status op=%02X rid=%u status=%u rev=%u\n",
             request_opcode | 0x80, request_id, status,
             s_rdx_hogpkm_current_revision);
    rdx_hogpkm_send_response(request_opcode,
                             request_id,
                             s_rdx_hogpkm_current_revision,
                             &status,
                             1,
                             generation);
}

static void rdx_hogpkm_send_queued_status(u32 packed_status, u32 generation)
{
    if (generation != s_rdx_hogpkm_generation) {
        HOGPKM_TRACE("[HOGPKM] drop stale queued status gen=%u curr=%u\n",
                     generation, s_rdx_hogpkm_generation);
        return;
    }
    rdx_hogpkm_send_status((u8)(packed_status >> 24),
                           (u16)(packed_status >> 8),
                           (u8)packed_status,
                           generation);
}

static int rdx_hogpkm_queue_status(u8 request_opcode, u16 request_id, u8 status)
{
    int msg[4];

    msg[0] = (int)rdx_hogpkm_send_queued_status;
    msg[1] = 2;
    msg[2] = (int)(((u32)request_opcode << 24) |
                   ((u32)request_id << 8) |
                   status);
    msg[3] = (int)s_rdx_hogpkm_generation;
    return os_taskq_post_type("app_core", Q_CALLBACK, 4, msg);
}

static void rdx_hogpkm_clear_pending_if_match(const rdx_hogpkm_request_t *request)
{
    if (s_rdx_hogpkm_pending.valid &&
        s_rdx_hogpkm_pending.generation == request->generation &&
        s_rdx_hogpkm_pending.request_id == request->request_id &&
        s_rdx_hogpkm_pending.request_frame_crc32 == request->request_frame_crc32) {
        memset(&s_rdx_hogpkm_pending, 0, sizeof(s_rdx_hogpkm_pending));
    }
}

static void rdx_hogpkm_payload_to_executor(const u8 *payload,
                                           rdx_hogp_key_action_keymap_t *keymap)
{
    u8 key;

    memset(keymap, 0, sizeof(*keymap));
    keymap->version = RDX_HOGPKM_VERSION;
    keymap->key_count = RDX_HOGPKM_KEY_COUNT;
    for (key = 0; key < RDX_HOGPKM_KEY_COUNT; key++) {
        const u8 *entry = &payload[key * RDX_HOGPKM_ENTRY_LEN];
        keymap->keys[key].modifiers = entry[0];
        memcpy(keymap->keys[key].usages,
               &entry[1],
               sizeof(keymap->keys[key].usages));
    }
}

static int rdx_hogpkm_apply_payload(const u8 *payload)
{
    static rdx_hogp_key_action_keymap_t keymap;

    rdx_hogpkm_payload_to_executor(payload, &keymap);
    return rdx_hogp_key_action_keymap_apply(&keymap);
}

static int rdx_hogpkm_commit(u32 generation,
                             u32 revision,
                             const u8 *payload,
                             u32 keymap_crc32)
{
    static u8 old_payload[RDX_HOGPKM_KEYMAP_LEN];
    static rdx_hogpkm_store_transaction_t transaction;
    u8 committed_slot;

    HOGPKM_TRACE("[HOGPKM] commit begin new_rev=%u old_slot=%u keymap_crc=%08X\n",
             revision, s_rdx_hogpkm_active_slot, keymap_crc32);
    memcpy(old_payload, s_rdx_hogpkm_current_keymap, sizeof(old_payload));
    if (rdx_hogpkm_store_prepare(s_rdx_hogpkm_active_slot,
                                 revision,
                                 payload,
                                 keymap_crc32,
                                 &transaction)) {
        HOGPKM_TRACE("[HOGPKM] commit prepare failed\n");
        return -1;
    }
    if (generation != s_rdx_hogpkm_generation) {
        return -3;
    }
    if (rdx_hogpkm_apply_payload(payload)) {
        HOGPKM_TRACE("[HOGPKM] commit apply failed\n");
        return -2;
    }
    HOGPKM_TRACE("[HOGPKM] commit apply ok\n");
    if (generation != s_rdx_hogpkm_generation) {
        rdx_hogpkm_apply_payload(old_payload);
        return -3;
    }
    if (rdx_hogpkm_store_commit(&transaction, &committed_slot)) {
        rdx_hogpkm_apply_payload(old_payload);
        HOGPKM_TRACE("[HOGPKM] commit vm failed, rollback applied\n");
        return -1;
    }

    memcpy(s_rdx_hogpkm_current_keymap, payload, RDX_HOGPKM_KEYMAP_LEN);
    s_rdx_hogpkm_current_revision = revision;
    s_rdx_hogpkm_current_keymap_crc32 = keymap_crc32;
    s_rdx_hogpkm_active_slot = committed_slot;
    HOGPKM_TRACE("[HOGPKM] commit ok rev=%u slot=%u keymap_crc=%08X\n",
             s_rdx_hogpkm_current_revision, s_rdx_hogpkm_active_slot,
             s_rdx_hogpkm_current_keymap_crc32);
    return (generation == s_rdx_hogpkm_generation) ? 0 : 1;
}

static void rdx_hogpkm_send_write_success(const rdx_hogpkm_request_t *request,
                                           u32 revision,
                                           u32 keymap_crc32)
{
    u8 payload[5];

    payload[0] = RDX_HOGPKM_STATUS_OK;
    rdx_hogpkm_put_le32(&payload[1], keymap_crc32);
    rdx_hogpkm_send_response(request->opcode,
                             request->request_id,
                             revision,
                             payload,
                             sizeof(payload),
                             request->generation);
}

static void rdx_hogpkm_cache_success(const rdx_hogpkm_request_t *request)
{
    s_rdx_hogpkm_cache.valid = 1;
    s_rdx_hogpkm_cache.opcode = request->opcode;
    s_rdx_hogpkm_cache.request_id = request->request_id;
    s_rdx_hogpkm_cache.request_frame_crc32 = request->request_frame_crc32;
    s_rdx_hogpkm_cache.revision = s_rdx_hogpkm_current_revision;
    s_rdx_hogpkm_cache.keymap_crc32 = s_rdx_hogpkm_current_keymap_crc32;
}

static int rdx_hogpkm_resend_cached(const rdx_hogpkm_request_t *request)
{
    if (!s_rdx_hogpkm_cache.valid ||
        s_rdx_hogpkm_cache.opcode != request->opcode ||
        s_rdx_hogpkm_cache.request_id != request->request_id ||
        s_rdx_hogpkm_cache.request_frame_crc32 != request->request_frame_crc32) {
        return 0;
    }

    HOGPKM_TRACE("[HOGPKM] resend cached op=%02X rid=%u rev=%u keymap_crc=%08X\n",
             request->opcode, request->request_id,
             s_rdx_hogpkm_cache.revision, s_rdx_hogpkm_cache.keymap_crc32);
    rdx_hogpkm_send_write_success(request,
                                  s_rdx_hogpkm_cache.revision,
                                  s_rdx_hogpkm_cache.keymap_crc32);
    return 1;
}

static u8 rdx_hogpkm_access_status(void)
{
    if (rdx_ble_connection_owner_get() != RDX_BLE_OWNER_CONFIG ||
#if TCFG_RDX_SESSION_AUTH_GATE_ENABLE
        !rdx_protocol_session_is_authorized(
            rdx_ble_server_get_info()->ble_con_handle) ||
#endif
        !rdx_hogp_keymap_product_authorized()) {
        return RDX_HOGPKM_STATUS_NOT_AUTHORIZED;
    }
    if (get_ota_status() || rdx_app_get_poweroff_flag() ||
        rdx_dut_is_in_mode()) {
        return RDX_HOGPKM_STATUS_BUSY;
    }
    return RDX_HOGPKM_STATUS_OK;
}

static void rdx_hogpkm_process_get_keymap(const rdx_hogpkm_request_t *request)
{
    static u8 payload[1 + RDX_HOGPKM_KEYMAP_LEN];

    HOGPKM_TRACE("[HOGPKM] get_keymap rid=%u rev=%u keymap_crc=%08X\n",
             request->request_id, s_rdx_hogpkm_current_revision,
             s_rdx_hogpkm_current_keymap_crc32);
    payload[0] = RDX_HOGPKM_STATUS_OK;
    memcpy(&payload[1], s_rdx_hogpkm_current_keymap, RDX_HOGPKM_KEYMAP_LEN);
    rdx_hogpkm_send_response(request->opcode,
                             request->request_id,
                             s_rdx_hogpkm_current_revision,
                             payload,
                             sizeof(payload),
                             request->generation);
}

static void rdx_hogpkm_process_get_caps(const rdx_hogpkm_request_t *request)
{
    const u8 payload[5] = {
        RDX_HOGPKM_STATUS_OK,
        RDX_HOGPKM_KEY_COUNT,
        6,
        0x03,
        0xFF,
    };

    HOGPKM_TRACE("[HOGPKM] get_caps rid=%u rev=%u\n",
             request->request_id, s_rdx_hogpkm_current_revision);
    rdx_hogpkm_send_response(request->opcode,
                             request->request_id,
                             s_rdx_hogpkm_current_revision,
                             payload,
                             sizeof(payload),
                             request->generation);
}

static void rdx_hogpkm_process_set_keymap(const rdx_hogpkm_request_t *request)
{
    u32 candidate_crc32;
    u8 status = rdx_hogpkm_validate_keymap(request->payload);
    int ret;

    HOGPKM_TRACE("[HOGPKM] set begin rid=%u base_rev=%u curr_rev=%u\n",
             request->request_id, request->base_revision,
             s_rdx_hogpkm_current_revision);
    if (status != RDX_HOGPKM_STATUS_OK) {
        HOGPKM_TRACE("[HOGPKM] set validate failed status=%u\n", status);
        rdx_hogpkm_send_status(request->opcode, request->request_id, status,
                               request->generation);
        return;
    }
    if (rdx_hogpkm_resend_cached(request)) {
        return;
    }

    candidate_crc32 = rdx_hogpkm_crc32(request->payload, RDX_HOGPKM_KEYMAP_LEN);
    HOGPKM_TRACE("[HOGPKM] set validate ok keymap_crc=%08X\n", candidate_crc32);
    rdx_hogpkm_log_keymap("set", request->payload);
    if (request->base_revision != s_rdx_hogpkm_current_revision &&
        candidate_crc32 == s_rdx_hogpkm_current_keymap_crc32 &&
        memcmp(request->payload,
               s_rdx_hogpkm_current_keymap,
               RDX_HOGPKM_KEYMAP_LEN) == 0) {
        HOGPKM_TRACE("[HOGPKM] set idempotent ok req_rev=%u curr_rev=%u\n",
                 request->base_revision, s_rdx_hogpkm_current_revision);
        rdx_hogpkm_cache_success(request);
        rdx_hogpkm_send_write_success(request,
                                      s_rdx_hogpkm_current_revision,
                                      s_rdx_hogpkm_current_keymap_crc32);
        return;
    }
    if (request->base_revision != s_rdx_hogpkm_current_revision) {
        HOGPKM_TRACE("[HOGPKM] set revision conflict req_rev=%u curr_rev=%u\n",
                 request->base_revision, s_rdx_hogpkm_current_revision);
        rdx_hogpkm_send_status(request->opcode,
                               request->request_id,
                               RDX_HOGPKM_STATUS_REVISION_CONFLICT,
                               request->generation);
        return;
    }
    if (s_rdx_hogpkm_current_revision == 0xFFFFFFFFU) {
        HOGPKM_TRACE("[HOGPKM] set revision overflow\n");
        rdx_hogpkm_send_status(request->opcode,
                               request->request_id,
                               RDX_HOGPKM_STATUS_INTERNAL_ERROR,
                               request->generation);
        return;
    }

    ret = rdx_hogpkm_commit(request->generation,
                            s_rdx_hogpkm_current_revision + 1,
                            request->payload,
                            candidate_crc32);
    if (ret == 1 || ret == -3) {
        return;
    }
    if (ret) {
        HOGPKM_TRACE("[HOGPKM] set commit failed ret=%d\n", ret);
        rdx_hogpkm_send_status(request->opcode,
                               request->request_id,
                               ret == -2 ? RDX_HOGPKM_STATUS_INTERNAL_ERROR :
                                           RDX_HOGPKM_STATUS_STORAGE_ERROR,
                               request->generation);
        return;
    }

    rdx_hogpkm_cache_success(request);
    HOGPKM_TRACE("[HOGPKM] set ok rid=%u rev=%u keymap_crc=%08X\n",
             request->request_id, s_rdx_hogpkm_current_revision,
             s_rdx_hogpkm_current_keymap_crc32);
    rdx_hogpkm_send_write_success(request,
                                  s_rdx_hogpkm_current_revision,
                                  s_rdx_hogpkm_current_keymap_crc32);
}

static void rdx_hogpkm_process_reset_keymap(const rdx_hogpkm_request_t *request)
{
    u32 default_crc32 = rdx_hogpkm_crc32(s_rdx_hogpkm_default_keymap,
                                         RDX_HOGPKM_KEYMAP_LEN);
    int ret;

    HOGPKM_TRACE("[HOGPKM] reset begin rid=%u base_rev=%u curr_rev=%u\n",
             request->request_id, request->base_revision,
             s_rdx_hogpkm_current_revision);
    if (rdx_hogpkm_resend_cached(request)) {
        return;
    }
    if (default_crc32 == s_rdx_hogpkm_current_keymap_crc32 &&
        memcmp(s_rdx_hogpkm_default_keymap,
               s_rdx_hogpkm_current_keymap,
               RDX_HOGPKM_KEYMAP_LEN) == 0 &&
        request->base_revision != s_rdx_hogpkm_current_revision) {
        HOGPKM_TRACE("[HOGPKM] reset idempotent ok req_rev=%u curr_rev=%u\n",
                 request->base_revision, s_rdx_hogpkm_current_revision);
        rdx_hogpkm_cache_success(request);
        rdx_hogpkm_send_write_success(request,
                                      s_rdx_hogpkm_current_revision,
                                      s_rdx_hogpkm_current_keymap_crc32);
        return;
    }
    if (request->base_revision != s_rdx_hogpkm_current_revision) {
        HOGPKM_TRACE("[HOGPKM] reset revision conflict req_rev=%u curr_rev=%u\n",
                 request->base_revision, s_rdx_hogpkm_current_revision);
        rdx_hogpkm_send_status(request->opcode,
                               request->request_id,
                               RDX_HOGPKM_STATUS_REVISION_CONFLICT,
                               request->generation);
        return;
    }
    if (s_rdx_hogpkm_current_revision == 0xFFFFFFFFU) {
        HOGPKM_TRACE("[HOGPKM] reset revision overflow\n");
        rdx_hogpkm_send_status(request->opcode,
                               request->request_id,
                               RDX_HOGPKM_STATUS_INTERNAL_ERROR,
                               request->generation);
        return;
    }

    ret = rdx_hogpkm_commit(request->generation,
                            s_rdx_hogpkm_current_revision + 1,
                            s_rdx_hogpkm_default_keymap,
                            default_crc32);
    if (ret == 1 || ret == -3) {
        return;
    }
    if (ret) {
        HOGPKM_TRACE("[HOGPKM] reset commit failed ret=%d\n", ret);
        rdx_hogpkm_send_status(request->opcode,
                               request->request_id,
                               ret == -2 ? RDX_HOGPKM_STATUS_INTERNAL_ERROR :
                                           RDX_HOGPKM_STATUS_STORAGE_ERROR,
                               request->generation);
        return;
    }

    rdx_hogpkm_cache_success(request);
    HOGPKM_TRACE("[HOGPKM] reset ok rid=%u rev=%u keymap_crc=%08X\n",
             request->request_id, s_rdx_hogpkm_current_revision,
             s_rdx_hogpkm_current_keymap_crc32);
    rdx_hogpkm_send_write_success(request,
                                  s_rdx_hogpkm_current_revision,
                                  s_rdx_hogpkm_current_keymap_crc32);
}

static void rdx_hogpkm_process_pending(void)
{
    static rdx_hogpkm_request_t request;
    u8 access_status;

    if (!s_rdx_hogpkm_pending.valid) {
        return;
    }
    memcpy(&request, &s_rdx_hogpkm_pending, sizeof(request));

    if (request.generation != s_rdx_hogpkm_generation) {
        HOGPKM_TRACE("[HOGPKM] pending stale op=%02X rid=%u req_gen=%u curr_gen=%u\n",
                 request.opcode, request.request_id,
                 request.generation, s_rdx_hogpkm_generation);
        rdx_hogpkm_clear_pending_if_match(&request);
        return;
    }
    access_status = rdx_hogpkm_access_status();
    if (access_status != RDX_HOGPKM_STATUS_OK) {
        HOGPKM_TRACE("[HOGPKM] access rejected op=%02X rid=%u owner=%d status=%u\n",
                 request.opcode, request.request_id,
                 rdx_ble_connection_owner_get(), access_status);
        rdx_hogpkm_send_status(request.opcode,
                               request.request_id,
                               access_status,
                               request.generation);
        rdx_hogpkm_clear_pending_if_match(&request);
        return;
    }

    HOGPKM_TRACE("[HOGPKM] process op=%02X rid=%u base_rev=%u payload_len=%u\n",
             request.opcode, request.request_id, request.base_revision,
             request.payload_len);
    switch (request.opcode) {
    case RDX_HOGPKM_OP_SET_KEYMAP:
        rdx_hogpkm_process_set_keymap(&request);
        break;
    case RDX_HOGPKM_OP_GET_KEYMAP:
        rdx_hogpkm_process_get_keymap(&request);
        break;
    case RDX_HOGPKM_OP_GET_CAPS:
        rdx_hogpkm_process_get_caps(&request);
        break;
    case RDX_HOGPKM_OP_RESET_KEYMAP:
        rdx_hogpkm_process_reset_keymap(&request);
        break;
    default:
        rdx_hogpkm_send_status(request.opcode,
                               request.request_id,
                               RDX_HOGPKM_STATUS_UNSUPPORTED_OPCODE,
                               request.generation);
        break;
    }

    rdx_hogpkm_clear_pending_if_match(&request);
}

void rdx_hogp_keymap_config_init(void)
{
    static rdx_hogpkm_store_entry_t entry;

    memset(&s_rdx_hogpkm_pending, 0, sizeof(s_rdx_hogpkm_pending));
    memset(&s_rdx_hogpkm_cache, 0, sizeof(s_rdx_hogpkm_cache));
    s_rdx_hogpkm_generation++;

    if (rdx_hogpkm_store_load(&entry) == 0) {
        memcpy(s_rdx_hogpkm_current_keymap, entry.payload, RDX_HOGPKM_KEYMAP_LEN);
        s_rdx_hogpkm_current_revision = entry.revision;
        s_rdx_hogpkm_current_keymap_crc32 = entry.keymap_crc32;
        s_rdx_hogpkm_active_slot = entry.slot;
    } else {
        memcpy(s_rdx_hogpkm_current_keymap,
               s_rdx_hogpkm_default_keymap,
               RDX_HOGPKM_KEYMAP_LEN);
        s_rdx_hogpkm_current_revision = 0;
        s_rdx_hogpkm_current_keymap_crc32 =
            rdx_hogpkm_crc32(s_rdx_hogpkm_current_keymap, RDX_HOGPKM_KEYMAP_LEN);
        s_rdx_hogpkm_active_slot = RDX_HOGPKM_VM_SLOT_NONE;
    }

    if (rdx_hogpkm_apply_payload(s_rdx_hogpkm_current_keymap)) {
        HOGPKM_TRACE("[HOGPKM] executor apply failed during init\n");
    }
    HOGPKM_TRACE("[HOGPKM] init revision=%u slot=%u keymap_crc=%08X\n",
             s_rdx_hogpkm_current_revision,
             s_rdx_hogpkm_active_slot,
             s_rdx_hogpkm_current_keymap_crc32);
}

void rdx_hogp_keymap_config_handle_custom(const char *value)
{
    static rdx_hogpkm_request_t request;
    u8 status;
    u8 can_respond;
    int msg[2];

    if (value == NULL) {
        HOGPKM_TRACE("[HOGPKM] rx null value\n");
        return;
    }
    if (rdx_hogpkm_decode_request(value, &request, &status, &can_respond)) {
        HOGPKM_TRACE("[HOGPKM] rx decode failed can_rsp=%u op=%02X rid=%u status=%u\n",
                 can_respond, request.opcode, request.request_id, status);
        if (can_respond) {
            if (rdx_hogpkm_queue_status(request.opcode, request.request_id, status)) {
                HOGPKM_TRACE("[HOGPKM] queue decode error response failed\n");
            }
        }
        return;
    }

    HOGPKM_TRACE("[HOGPKM] rx ok op=%02X rid=%u base_rev=%u payload_len=%u frame_crc=%08X curr_rev=%u\n",
             request.opcode, request.request_id, request.base_revision,
             request.payload_len, request.request_frame_crc32,
             s_rdx_hogpkm_current_revision);
    request.generation = s_rdx_hogpkm_generation;
    if (s_rdx_hogpkm_pending.valid) {
        if (s_rdx_hogpkm_pending.request_id == request.request_id &&
            s_rdx_hogpkm_pending.request_frame_crc32 != request.request_frame_crc32) {
            status = RDX_HOGPKM_STATUS_REQUEST_ID_CONFLICT;
        } else {
            status = RDX_HOGPKM_STATUS_BUSY;
        }
        HOGPKM_TRACE("[HOGPKM] pending busy new_rid=%u pending_rid=%u status=%u\n",
                 request.request_id, s_rdx_hogpkm_pending.request_id, status);
        if (rdx_hogpkm_queue_status(request.opcode, request.request_id, status)) {
            HOGPKM_TRACE("[HOGPKM] queue busy response failed\n");
        }
        return;
    }

    memcpy(&s_rdx_hogpkm_pending, &request, sizeof(request));
    s_rdx_hogpkm_pending.valid = 1;
    msg[0] = (int)rdx_hogpkm_process_pending;
    msg[1] = 0;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 2, msg)) {
        memset(&s_rdx_hogpkm_pending, 0, sizeof(s_rdx_hogpkm_pending));
        HOGPKM_TRACE("[HOGPKM] post app_core failed op=%02X rid=%u\n",
                 request.opcode, request.request_id);
        /* A full app_core queue cannot safely accept a second callback. The
         * APP timeout/retry path will recover this request. */
        return;
    }
    HOGPKM_TRACE("[HOGPKM] queued app_core op=%02X rid=%u gen=%u\n",
             request.opcode, request.request_id, request.generation);
}

void rdx_hogp_keymap_config_on_disconnect(void)
{
    HOGPKM_TRACE("[HOGPKM] disconnect clear pending/cache gen=%u\n",
             s_rdx_hogpkm_generation + 1);
    s_rdx_hogpkm_generation++;
    memset(&s_rdx_hogpkm_pending, 0, sizeof(s_rdx_hogpkm_pending));
    memset(&s_rdx_hogpkm_cache, 0, sizeof(s_rdx_hogpkm_cache));
}

#else

void rdx_hogp_keymap_config_init(void) {}
void rdx_hogp_keymap_config_handle_custom(const char *value) { (void)value; }
void rdx_hogp_keymap_config_on_disconnect(void) {}
__attribute__((weak)) int rdx_hogp_keymap_product_authorized(void) { return 0; }

#endif
