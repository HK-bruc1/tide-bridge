#ifndef _RDX_HOGP_KEYMAP_INTERNAL_H_
#define _RDX_HOGP_KEYMAP_INTERNAL_H_

#include "system/includes.h"

#define RDX_HOGPKM_VERSION                         0x01

#define RDX_HOGPKM_OP_SET_KEYMAP                   0x01
#define RDX_HOGPKM_OP_GET_KEYMAP                   0x02
#define RDX_HOGPKM_OP_GET_CAPS                     0x03
#define RDX_HOGPKM_OP_RESET_KEYMAP                 0x04

#define RDX_HOGPKM_STATUS_OK                       0x00
#define RDX_HOGPKM_STATUS_UNSUPPORTED_VERSION      0x01
#define RDX_HOGPKM_STATUS_UNSUPPORTED_OPCODE       0x02
#define RDX_HOGPKM_STATUS_MALFORMED_FRAME          0x03
#define RDX_HOGPKM_STATUS_CRC_ERROR                0x04
#define RDX_HOGPKM_STATUS_REVISION_CONFLICT        0x05
#define RDX_HOGPKM_STATUS_INVALID_KEYMAP           0x06
#define RDX_HOGPKM_STATUS_INVALID_HID_USAGE        0x07
#define RDX_HOGPKM_STATUS_STORAGE_ERROR            0x08
#define RDX_HOGPKM_STATUS_NOT_AUTHORIZED           0x09
#define RDX_HOGPKM_STATUS_BUSY                     0x0A
#define RDX_HOGPKM_STATUS_INTERNAL_ERROR           0x0B
#define RDX_HOGPKM_STATUS_REQUEST_ID_CONFLICT      0x0C

#define RDX_HOGPKM_HEADER_LEN                      10
#define RDX_HOGPKM_CRC_LEN                         4
#define RDX_HOGPKM_KEY_COUNT                       5
#define RDX_HOGPKM_ENTRY_LEN                       7
#define RDX_HOGPKM_KEYMAP_LEN                      35
#define RDX_HOGPKM_MAX_REQUEST_FRAME_LEN           49
#define RDX_HOGPKM_MAX_REQUEST_HEX_LEN             98
#define RDX_HOGPKM_MAX_RESPONSE_FRAME_LEN          50
#define RDX_HOGPKM_MAX_RESPONSE_HEX_LEN            100
#define RDX_HOGPKM_VM_SLOT_NONE                    0xFF

typedef struct {
    u8 valid;
    u8 opcode;
    u16 request_id;
    u32 request_frame_crc32;
    u32 base_revision;
    u16 payload_len;
    u8 payload[RDX_HOGPKM_KEYMAP_LEN];
    u32 generation;
} rdx_hogpkm_request_t;

typedef struct {
    u8 valid;
    u8 slot;
    u32 revision;
    u32 keymap_crc32;
    u8 payload[RDX_HOGPKM_KEYMAP_LEN];
} rdx_hogpkm_store_entry_t;

typedef struct {
    u8 prepared;
    u8 slot;
    u32 revision;
    u32 keymap_crc32;
    u8 payload[RDX_HOGPKM_KEYMAP_LEN];
} rdx_hogpkm_store_transaction_t;

u16 rdx_hogpkm_get_le16(const u8 *p);
u32 rdx_hogpkm_get_le32(const u8 *p);
void rdx_hogpkm_put_le16(u8 *p, u16 value);
void rdx_hogpkm_put_le32(u8 *p, u32 value);
u32 rdx_hogpkm_crc32(const u8 *data, u16 len);

u8 rdx_hogpkm_validate_keymap(const u8 *payload);
int rdx_hogpkm_decode_request(const char *value,
                              rdx_hogpkm_request_t *request,
                              u8 *status,
                              u8 *can_respond);
int rdx_hogpkm_encode_response(u8 request_opcode,
                               u16 request_id,
                               u32 revision,
                               const u8 *payload,
                               u16 payload_len,
                               char *hex,
                               u16 hex_size);

int rdx_hogpkm_store_load(rdx_hogpkm_store_entry_t *entry);
int rdx_hogpkm_store_prepare(u8 active_slot,
                             u32 revision,
                             const u8 *payload,
                             u32 keymap_crc32,
                             rdx_hogpkm_store_transaction_t *transaction);
int rdx_hogpkm_store_commit(rdx_hogpkm_store_transaction_t *transaction,
                            u8 *committed_slot);

#endif /* _RDX_HOGP_KEYMAP_INTERNAL_H_ */
