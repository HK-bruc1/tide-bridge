/* Power-loss-safe A/B VM storage for the A1 keymap. */

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_keymap_store.data.bss")
#pragma data_seg(".rdx_hogp_keymap_store.data")
#pragma const_seg(".rdx_hogp_keymap_store.text.const")
#pragma code_seg(".rdx_hogp_keymap_store.text")
#endif

#include "app_config.h"
#include "system/includes.h"
#include "syscfg_id.h"

#include "rdx_app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_keymap_internal.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define RDX_HOGPKM_VM_DATA_LEN                     51
#define RDX_HOGPKM_VM_COMMIT_LEN                   20
#define RDX_HOGPKM_VM_DATA_MAGIC                   "HKM1"
#define RDX_HOGPKM_VM_COMMIT_MAGIC                 "HKC1"
#define RDX_HOGPKM_VM_SCHEMA                       0x01

static u16 rdx_hogpkm_vm_data_id(u8 slot)
{
    return slot ? VM_RDX_HOGP_KEYMAP_SLOT_B : VM_RDX_HOGP_KEYMAP_SLOT_A;
}

static u16 rdx_hogpkm_vm_commit_id(u8 slot)
{
    return slot ? VM_RDX_HOGP_KEYMAP_COMMIT_B : VM_RDX_HOGP_KEYMAP_COMMIT_A;
}

static void rdx_hogpkm_vm_build_data(u8 *record, u32 revision, const u8 *payload)
{
    memcpy(&record[0], RDX_HOGPKM_VM_DATA_MAGIC, 4);
    record[4] = RDX_HOGPKM_VM_SCHEMA;
    record[5] = 0;
    rdx_hogpkm_put_le16(&record[6], RDX_HOGPKM_KEYMAP_LEN);
    rdx_hogpkm_put_le32(&record[8], revision);
    memcpy(&record[12], payload, RDX_HOGPKM_KEYMAP_LEN);
    rdx_hogpkm_put_le32(&record[47], rdx_hogpkm_crc32(record, 47));
}

static int rdx_hogpkm_vm_validate_data(const u8 *record,
                                       u32 *revision,
                                       u32 *keymap_crc32,
                                       u8 *payload)
{
    if (memcmp(&record[0], RDX_HOGPKM_VM_DATA_MAGIC, 4) != 0 ||
        record[4] != RDX_HOGPKM_VM_SCHEMA ||
        record[5] != 0 ||
        rdx_hogpkm_get_le16(&record[6]) != RDX_HOGPKM_KEYMAP_LEN) {
        return -1;
    }

    *revision = rdx_hogpkm_get_le32(&record[8]);
    if (*revision == 0 ||
        rdx_hogpkm_get_le32(&record[47]) != rdx_hogpkm_crc32(record, 47) ||
        rdx_hogpkm_validate_keymap(&record[12]) != RDX_HOGPKM_STATUS_OK) {
        return -1;
    }

    memcpy(payload, &record[12], RDX_HOGPKM_KEYMAP_LEN);
    *keymap_crc32 = rdx_hogpkm_crc32(payload, RDX_HOGPKM_KEYMAP_LEN);
    return 0;
}

static void rdx_hogpkm_vm_build_commit(u8 *record,
                                       u8 slot,
                                       u32 revision,
                                       u32 keymap_crc32)
{
    memcpy(&record[0], RDX_HOGPKM_VM_COMMIT_MAGIC, 4);
    record[4] = RDX_HOGPKM_VM_SCHEMA;
    record[5] = slot;
    record[6] = 0;
    record[7] = 0;
    rdx_hogpkm_put_le32(&record[8], revision);
    rdx_hogpkm_put_le32(&record[12], keymap_crc32);
    rdx_hogpkm_put_le32(&record[16], rdx_hogpkm_crc32(record, 16));
}

static int rdx_hogpkm_vm_validate_commit(const u8 *record,
                                         u8 slot,
                                         u32 revision,
                                         u32 keymap_crc32)
{
    if (memcmp(&record[0], RDX_HOGPKM_VM_COMMIT_MAGIC, 4) != 0 ||
        record[4] != RDX_HOGPKM_VM_SCHEMA ||
        record[5] != slot ||
        record[6] != 0 ||
        record[7] != 0 ||
        rdx_hogpkm_get_le32(&record[8]) != revision ||
        rdx_hogpkm_get_le32(&record[12]) != keymap_crc32 ||
        rdx_hogpkm_get_le32(&record[16]) != rdx_hogpkm_crc32(record, 16)) {
        return -1;
    }
    return 0;
}

static int rdx_hogpkm_vm_read_slot(u8 slot, rdx_hogpkm_store_entry_t *out)
{
    static u8 data_record[RDX_HOGPKM_VM_DATA_LEN];
    static u8 commit_record[RDX_HOGPKM_VM_COMMIT_LEN];

    memset(out, 0, sizeof(*out));
    if (syscfg_read(rdx_hogpkm_vm_data_id(slot), data_record, sizeof(data_record)) != sizeof(data_record) ||
        rdx_hogpkm_vm_validate_data(data_record,
                                    &out->revision,
                                    &out->keymap_crc32,
                                    out->payload) ||
        syscfg_read(rdx_hogpkm_vm_commit_id(slot), commit_record, sizeof(commit_record)) != sizeof(commit_record) ||
        rdx_hogpkm_vm_validate_commit(commit_record,
                                      slot,
                                      out->revision,
                                      out->keymap_crc32)) {
        return -1;
    }

    out->slot = slot;
    out->valid = 1;
    return 0;
}

int rdx_hogpkm_store_load(rdx_hogpkm_store_entry_t *entry)
{
    static rdx_hogpkm_store_entry_t slot_a;
    static rdx_hogpkm_store_entry_t slot_b;
    rdx_hogpkm_store_entry_t *selected = NULL;

    rdx_hogpkm_vm_read_slot(0, &slot_a);
    rdx_hogpkm_vm_read_slot(1, &slot_b);

    if (slot_a.valid && slot_b.valid) {
        if (slot_a.revision > slot_b.revision) {
            selected = &slot_a;
        } else if (slot_b.revision > slot_a.revision) {
            selected = &slot_b;
        } else if (memcmp(slot_a.payload, slot_b.payload, RDX_HOGPKM_KEYMAP_LEN) == 0) {
            selected = &slot_a;
        } else {
            y_printf("[HOGPKM] VM conflict at revision %u\n", slot_a.revision);
        }
    } else if (slot_a.valid) {
        selected = &slot_a;
    } else if (slot_b.valid) {
        selected = &slot_b;
    }

    if (selected == NULL) {
        memset(entry, 0, sizeof(*entry));
        entry->slot = RDX_HOGPKM_VM_SLOT_NONE;
        return 1;
    }

    memcpy(entry, selected, sizeof(*entry));
    return 0;
}

int rdx_hogpkm_store_prepare(u8 active_slot,
                             u32 revision,
                             const u8 *payload,
                             u32 keymap_crc32,
                             rdx_hogpkm_store_transaction_t *transaction)
{
    static u8 data_record[RDX_HOGPKM_VM_DATA_LEN];
    static u8 data_readback[RDX_HOGPKM_VM_DATA_LEN];
    static u8 verify_payload[RDX_HOGPKM_KEYMAP_LEN];
    u8 target_slot = (active_slot == 0) ? 1 : 0;
    u32 verify_revision;
    u32 verify_keymap_crc32;

    if (transaction == NULL || payload == NULL) {
        return -1;
    }
    memset(transaction, 0, sizeof(*transaction));

    rdx_hogpkm_vm_build_data(data_record, revision, payload);
    if (syscfg_write(rdx_hogpkm_vm_data_id(target_slot), data_record, sizeof(data_record)) != sizeof(data_record) ||
        syscfg_read(rdx_hogpkm_vm_data_id(target_slot), data_readback, sizeof(data_readback)) != sizeof(data_readback) ||
        memcmp(data_record, data_readback, sizeof(data_record)) != 0 ||
        rdx_hogpkm_vm_validate_data(data_readback,
                                    &verify_revision,
                                    &verify_keymap_crc32,
                                    verify_payload) ||
        verify_revision != revision ||
        verify_keymap_crc32 != keymap_crc32 ||
        memcmp(verify_payload, payload, RDX_HOGPKM_KEYMAP_LEN) != 0) {
        return -1;
    }

    transaction->prepared = 1;
    transaction->slot = target_slot;
    transaction->revision = revision;
    transaction->keymap_crc32 = keymap_crc32;
    memcpy(transaction->payload, payload, RDX_HOGPKM_KEYMAP_LEN);
    return 0;
}

int rdx_hogpkm_store_commit(rdx_hogpkm_store_transaction_t *transaction,
                            u8 *committed_slot)
{
    static u8 commit_record[RDX_HOGPKM_VM_COMMIT_LEN];
    static u8 commit_readback[RDX_HOGPKM_VM_COMMIT_LEN];
    static rdx_hogpkm_store_entry_t verified_slot;
    int commit_valid = 0;

    if (transaction == NULL || committed_slot == NULL || !transaction->prepared) {
        return -1;
    }

    rdx_hogpkm_vm_build_commit(commit_record,
                               transaction->slot,
                               transaction->revision,
                               transaction->keymap_crc32);
    if (syscfg_write(rdx_hogpkm_vm_commit_id(transaction->slot), commit_record, sizeof(commit_record)) == sizeof(commit_record) &&
        syscfg_read(rdx_hogpkm_vm_commit_id(transaction->slot), commit_readback, sizeof(commit_readback)) == sizeof(commit_readback) &&
        memcmp(commit_record, commit_readback, sizeof(commit_record)) == 0 &&
        rdx_hogpkm_vm_validate_commit(commit_readback,
                                      transaction->slot,
                                      transaction->revision,
                                      transaction->keymap_crc32) == 0) {
        commit_valid = 1;
    }

    if (!commit_valid &&
        rdx_hogpkm_vm_read_slot(transaction->slot, &verified_slot) == 0 &&
        verified_slot.revision == transaction->revision &&
        verified_slot.keymap_crc32 == transaction->keymap_crc32 &&
        memcmp(verified_slot.payload,
               transaction->payload,
               RDX_HOGPKM_KEYMAP_LEN) == 0) {
        commit_valid = 1;
    }

    if (!commit_valid) {
        return -1;
    }

    *committed_slot = transaction->slot;
    transaction->prepared = 0;
    return 0;
}

#endif
