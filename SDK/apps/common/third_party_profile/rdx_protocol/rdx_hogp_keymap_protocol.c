/* A1 wire codec and transport-neutral keymap validation. */

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_keymap_protocol.data.bss")
#pragma data_seg(".rdx_hogp_keymap_protocol.data")
#pragma const_seg(".rdx_hogp_keymap_protocol.text.const")
#pragma code_seg(".rdx_hogp_keymap_protocol.text")
#endif

#include "app_config.h"
#include "system/includes.h"

#include "device/hid/hid_keyboard_usage.h"
#include "rdx_app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_keymap_internal.h"
#include "rdx_util.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

u16 rdx_hogpkm_get_le16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

u32 rdx_hogpkm_get_le32(const u8 *p)
{
    return (u32)p[0] |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

void rdx_hogpkm_put_le16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

void rdx_hogpkm_put_le32(u8 *p, u32 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
}

u32 rdx_hogpkm_crc32(const u8 *data, u16 len)
{
    return rdx_util_crc32((u8 *)data, len, NULL);
}

static u16 rdx_hogpkm_bounded_strlen(const char *value, u16 limit)
{
    u16 len = 0;

    while (len < limit && value[len] != '\0') {
        len++;
    }
    return len;
}

static int rdx_hogpkm_hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int rdx_hogpkm_hex_decode(const char *hex, u16 hex_len, u8 *out)
{
    u16 i;

    for (i = 0; i < hex_len; i += 2) {
        int hi = rdx_hogpkm_hex_nibble(hex[i]);
        int lo = rdx_hogpkm_hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i / 2] = (u8)((hi << 4) | lo);
    }
    return 0;
}

static void rdx_hogpkm_hex_encode(const u8 *data, u16 len, char *out)
{
    static const char digits[] = "0123456789ABCDEF";
    u16 i;

    for (i = 0; i < len; i++) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static int rdx_hogpkm_usage_is_supported(u8 usage)
{
    if (usage >= HID_KEYBOARD_USAGE_STANDARD_MIN &&
        usage <= HID_KEYBOARD_USAGE_STANDARD_MAX) {
        return 1;
    }
    if (usage >= HID_KEYBOARD_USAGE_KEYPAD_EXT_MIN &&
        usage <= HID_KEYBOARD_USAGE_KEYPAD_EXT_MAX) {
        return 1;
    }
    return 0;
}

u8 rdx_hogpkm_validate_keymap(const u8 *payload)
{
    u8 key;

    for (key = 0; key < RDX_HOGPKM_KEY_COUNT; key++) {
        const u8 *usages = &payload[key * RDX_HOGPKM_ENTRY_LEN + 1];
        u8 seen_zero = 0;
        u8 i;
        u8 j;

        for (i = 0; i < 6; i++) {
            u8 usage = usages[i];
            if (usage == HID_KEYBOARD_USAGE_NONE) {
                seen_zero = 1;
                continue;
            }
            if (seen_zero) {
                return RDX_HOGPKM_STATUS_INVALID_KEYMAP;
            }
            if (!rdx_hogpkm_usage_is_supported(usage)) {
                return RDX_HOGPKM_STATUS_INVALID_HID_USAGE;
            }
            for (j = 0; j < i; j++) {
                if (usages[j] == usage) {
                    return RDX_HOGPKM_STATUS_INVALID_KEYMAP;
                }
            }
        }
    }
    return RDX_HOGPKM_STATUS_OK;
}

int rdx_hogpkm_decode_request(const char *value,
                              rdx_hogpkm_request_t *request,
                              u8 *status,
                              u8 *can_respond)
{
    /* Custom command parsing is serialized by the RDX receive path. Keeping
     * the frame off its small task stack also protects malformed long input. */
    static u8 frame[RDX_HOGPKM_MAX_REQUEST_FRAME_LEN];
    u16 hex_len = rdx_hogpkm_bounded_strlen(value, RDX_HOGPKM_MAX_REQUEST_HEX_LEN + 1);
    u16 frame_len;
    u16 expected_len;
    u32 received_crc;

    memset(request, 0, sizeof(*request));
    *status = RDX_HOGPKM_STATUS_MALFORMED_FRAME;
    *can_respond = 0;

    if (hex_len >= 8 && rdx_hogpkm_hex_decode(value, 8, frame) == 0) {
        request->opcode = frame[1];
        request->request_id = rdx_hogpkm_get_le16(&frame[2]);
        *can_respond = 1;
    }
    if (hex_len > RDX_HOGPKM_MAX_REQUEST_HEX_LEN ||
        hex_len < (RDX_HOGPKM_HEADER_LEN + RDX_HOGPKM_CRC_LEN) * 2 ||
        (hex_len & 1)) {
        return -1;
    }

    frame_len = hex_len / 2;
    if (rdx_hogpkm_hex_decode(value, hex_len, frame)) {
        return -1;
    }

    request->opcode = frame[1];
    request->request_id = rdx_hogpkm_get_le16(&frame[2]);
    request->base_revision = rdx_hogpkm_get_le32(&frame[4]);
    request->payload_len = rdx_hogpkm_get_le16(&frame[8]);
    *can_respond = 1;

    expected_len = RDX_HOGPKM_HEADER_LEN + request->payload_len + RDX_HOGPKM_CRC_LEN;
    if (expected_len != frame_len || request->payload_len > RDX_HOGPKM_KEYMAP_LEN) {
        return -1;
    }

    received_crc = rdx_hogpkm_get_le32(&frame[frame_len - RDX_HOGPKM_CRC_LEN]);
    request->request_frame_crc32 = received_crc;
    if (received_crc != rdx_hogpkm_crc32(frame, frame_len - RDX_HOGPKM_CRC_LEN)) {
        *status = RDX_HOGPKM_STATUS_CRC_ERROR;
        return -1;
    }
    if (frame[0] != RDX_HOGPKM_VERSION) {
        *status = RDX_HOGPKM_STATUS_UNSUPPORTED_VERSION;
        return -1;
    }

    switch (request->opcode) {
    case RDX_HOGPKM_OP_SET_KEYMAP:
        if (request->payload_len != RDX_HOGPKM_KEYMAP_LEN) {
            *status = RDX_HOGPKM_STATUS_INVALID_KEYMAP;
            return -1;
        }
        break;
    case RDX_HOGPKM_OP_GET_KEYMAP:
    case RDX_HOGPKM_OP_GET_CAPS:
        if (request->payload_len != 0 || request->base_revision != 0) {
            return -1;
        }
        break;
    case RDX_HOGPKM_OP_RESET_KEYMAP:
        if (request->payload_len != 0) {
            return -1;
        }
        break;
    default:
        *status = RDX_HOGPKM_STATUS_UNSUPPORTED_OPCODE;
        return -1;
    }

    if (request->payload_len) {
        memcpy(request->payload, &frame[RDX_HOGPKM_HEADER_LEN], request->payload_len);
    }
    return 0;
}

int rdx_hogpkm_encode_response(u8 request_opcode,
                               u16 request_id,
                               u32 revision,
                               const u8 *payload,
                               u16 payload_len,
                               char *hex,
                               u16 hex_size)
{
    /* Responses are serialized on app_core by the service. */
    static u8 frame[RDX_HOGPKM_MAX_RESPONSE_FRAME_LEN];
    u16 frame_len = RDX_HOGPKM_HEADER_LEN + payload_len + RDX_HOGPKM_CRC_LEN;

    if (frame_len > sizeof(frame) || hex_size <= frame_len * 2) {
        return -1;
    }

    frame[0] = RDX_HOGPKM_VERSION;
    frame[1] = request_opcode | 0x80;
    rdx_hogpkm_put_le16(&frame[2], request_id);
    rdx_hogpkm_put_le32(&frame[4], revision);
    rdx_hogpkm_put_le16(&frame[8], payload_len);
    if (payload_len) {
        memcpy(&frame[RDX_HOGPKM_HEADER_LEN], payload, payload_len);
    }
    rdx_hogpkm_put_le32(&frame[RDX_HOGPKM_HEADER_LEN + payload_len],
                        rdx_hogpkm_crc32(frame, RDX_HOGPKM_HEADER_LEN + payload_len));
    rdx_hogpkm_hex_encode(frame, frame_len, hex);
    return frame_len * 2;
}

#endif
