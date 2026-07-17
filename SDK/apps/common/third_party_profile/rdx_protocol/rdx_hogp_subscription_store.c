/* Power-loss-tolerant, peer-scoped persistence for bonded HOGP CCC intent. */

#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_hogp_subscription_store.data.bss")
#pragma data_seg(".rdx_hogp_subscription_store.data")
#pragma const_seg(".rdx_hogp_subscription_store.text.const")
#pragma code_seg(".rdx_hogp_subscription_store.text")
#endif

#include "app_config.h"
#include "system/includes.h"
#include "syscfg_id.h"

#include "rdx_app_config.h"
#include "rdx_hogp_config.h"
#include "rdx_hogp_subscription_store.h"

#if TCFG_RDX_HOGP_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define RDX_HOGP_SUB_VM_MAGIC               "HCS1"
#define RDX_HOGP_SUB_VM_SCHEMA              0x01
#define RDX_HOGP_SUB_VM_SLOT_COUNT          4
#define RDX_HOGP_SUB_VM_RECORD_LEN          48
#define RDX_HOGP_SUB_VM_HEADER_LEN          12
#define RDX_HOGP_SUB_VM_ENTRY_LEN           8
#define RDX_HOGP_SUB_VM_CRC_OFFSET          44
#define RDX_HOGP_SUB_VM_SLOT_NONE           0xff

typedef struct {
    u8 valid;
    u8 peer_addr[RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN];
} rdx_hogp_subscription_entry_t;

typedef struct {
    u8 loaded;
    u8 active_vm_slot;
    u32 revision;
    rdx_hogp_subscription_entry_t entries[RDX_HOGP_SUB_VM_SLOT_COUNT];
} rdx_hogp_subscription_cache_t;

static rdx_hogp_subscription_cache_t s_hogp_subscription_cache;

static u16 rdx_hogp_subscription_vm_id(u8 slot)
{
    return slot ? VM_RDX_HOGP_SUBSCRIPTION_B : VM_RDX_HOGP_SUBSCRIPTION_A;
}

static u32 rdx_hogp_subscription_get_le32(const u8 *data)
{
    return ((u32)data[0]) |
           ((u32)data[1] << 8) |
           ((u32)data[2] << 16) |
           ((u32)data[3] << 24);
}

static void rdx_hogp_subscription_put_le32(u8 *data, u32 value)
{
    data[0] = value & 0xff;
    data[1] = (value >> 8) & 0xff;
    data[2] = (value >> 16) & 0xff;
    data[3] = (value >> 24) & 0xff;
}

static u32 rdx_hogp_subscription_crc32(const u8 *data, u16 len)
{
    u32 crc = 0xffffffff;
    u16 i;
    u8 bit;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320 & (0 - (crc & 1)));
        }
    }
    return ~crc;
}

static u8 rdx_hogp_subscription_peer_valid(const u8 *peer_addr)
{
    u8 all_zero = 1;
    u8 all_ff = 1;
    u8 i;

    if (peer_addr == NULL) {
        return 0;
    }
    for (i = 0; i < RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN; i++) {
        if (peer_addr[i] != 0x00) {
            all_zero = 0;
        }
        if (peer_addr[i] != 0xff) {
            all_ff = 0;
        }
    }
    return (!all_zero && !all_ff) ? 1 : 0;
}

static void rdx_hogp_subscription_build_record(
    u8 *record,
    const rdx_hogp_subscription_cache_t *cache,
    u32 revision)
{
    u8 i;

    memset(record, 0, RDX_HOGP_SUB_VM_RECORD_LEN);
    memcpy(&record[0], RDX_HOGP_SUB_VM_MAGIC, 4);
    record[4] = RDX_HOGP_SUB_VM_SCHEMA;
    record[5] = RDX_HOGP_SUB_VM_SLOT_COUNT;
    rdx_hogp_subscription_put_le32(&record[8], revision);

    for (i = 0; i < RDX_HOGP_SUB_VM_SLOT_COUNT; i++) {
        u16 offset = RDX_HOGP_SUB_VM_HEADER_LEN +
                     (i * RDX_HOGP_SUB_VM_ENTRY_LEN);
        record[offset] = cache->entries[i].valid ? 1 : 0;
        if (cache->entries[i].valid) {
            memcpy(&record[offset + 2], cache->entries[i].peer_addr,
                   RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN);
        }
    }
    rdx_hogp_subscription_put_le32(
        &record[RDX_HOGP_SUB_VM_CRC_OFFSET],
        rdx_hogp_subscription_crc32(record, RDX_HOGP_SUB_VM_CRC_OFFSET));
}

static int rdx_hogp_subscription_parse_record(
    const u8 *record,
    rdx_hogp_subscription_cache_t *cache)
{
    u8 i;
    u8 j;

    if (memcmp(&record[0], RDX_HOGP_SUB_VM_MAGIC, 4) != 0 ||
        record[4] != RDX_HOGP_SUB_VM_SCHEMA ||
        record[5] != RDX_HOGP_SUB_VM_SLOT_COUNT ||
        record[6] != 0 || record[7] != 0 ||
        rdx_hogp_subscription_get_le32(&record[RDX_HOGP_SUB_VM_CRC_OFFSET]) !=
            rdx_hogp_subscription_crc32(record, RDX_HOGP_SUB_VM_CRC_OFFSET)) {
        return -1;
    }

    memset(cache, 0, sizeof(*cache));
    cache->revision = rdx_hogp_subscription_get_le32(&record[8]);
    if (cache->revision == 0) {
        return -1;
    }

    for (i = 0; i < RDX_HOGP_SUB_VM_SLOT_COUNT; i++) {
        u16 offset = RDX_HOGP_SUB_VM_HEADER_LEN +
                     (i * RDX_HOGP_SUB_VM_ENTRY_LEN);
        if (record[offset] > 1 || record[offset + 1] != 0) {
            return -1;
        }
        cache->entries[i].valid = record[offset];
        if (!cache->entries[i].valid) {
            for (j = 0; j < RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN; j++) {
                if (record[offset + 2 + j] != 0) {
                    return -1;
                }
            }
            continue;
        }
        memcpy(cache->entries[i].peer_addr, &record[offset + 2],
               RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN);
        if (!rdx_hogp_subscription_peer_valid(cache->entries[i].peer_addr)) {
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (cache->entries[j].valid &&
                memcmp(cache->entries[j].peer_addr,
                       cache->entries[i].peer_addr,
                       RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN) == 0) {
                return -1;
            }
        }
    }
    cache->loaded = 1;
    return 0;
}

static int rdx_hogp_subscription_read_vm_slot(
    u8 slot,
    rdx_hogp_subscription_cache_t *cache)
{
    static u8 record[RDX_HOGP_SUB_VM_RECORD_LEN];

    if (syscfg_read(rdx_hogp_subscription_vm_id(slot), record,
                    sizeof(record)) != sizeof(record) ||
        rdx_hogp_subscription_parse_record(record, cache)) {
        memset(cache, 0, sizeof(*cache));
        return -1;
    }
    cache->active_vm_slot = slot;
    return 0;
}

static void rdx_hogp_subscription_load(void)
{
    static rdx_hogp_subscription_cache_t slot_a;
    static rdx_hogp_subscription_cache_t slot_b;
    const rdx_hogp_subscription_cache_t *selected = NULL;
    u32 baseline_revision = 0;

    if (s_hogp_subscription_cache.loaded) {
        return;
    }

    rdx_hogp_subscription_read_vm_slot(0, &slot_a);
    rdx_hogp_subscription_read_vm_slot(1, &slot_b);
    if (slot_a.loaded) {
        baseline_revision = slot_a.revision;
    }
    if (slot_b.loaded && slot_b.revision > baseline_revision) {
        baseline_revision = slot_b.revision;
    }
    if (slot_a.loaded && slot_b.loaded) {
        if (slot_a.revision > slot_b.revision) {
            selected = &slot_a;
        } else if (slot_b.revision > slot_a.revision) {
            selected = &slot_b;
        } else if (memcmp(slot_a.entries, slot_b.entries,
                          sizeof(slot_a.entries)) == 0) {
            selected = &slot_a;
        } else {
            y_printf("[HOGP_SUB] VM conflict revision=%u\n", slot_a.revision);
        }
    } else if (slot_a.loaded) {
        selected = &slot_a;
    } else if (slot_b.loaded) {
        selected = &slot_b;
    }

    memset(&s_hogp_subscription_cache, 0,
           sizeof(s_hogp_subscription_cache));
    s_hogp_subscription_cache.active_vm_slot = RDX_HOGP_SUB_VM_SLOT_NONE;
    s_hogp_subscription_cache.loaded = 1;
    s_hogp_subscription_cache.revision = baseline_revision;
    if (selected != NULL) {
        memcpy(&s_hogp_subscription_cache, selected,
               sizeof(s_hogp_subscription_cache));
    }
}

static int rdx_hogp_subscription_publish(
    const rdx_hogp_subscription_cache_t *candidate)
{
    static u8 record[RDX_HOGP_SUB_VM_RECORD_LEN];
    static u8 readback[RDX_HOGP_SUB_VM_RECORD_LEN];
    static rdx_hogp_subscription_cache_t verified;
    u8 target_slot;
    u32 next_revision;

    if (s_hogp_subscription_cache.revision == 0xffffffff) {
        return -1;
    }
    next_revision = s_hogp_subscription_cache.revision + 1;
    target_slot = (s_hogp_subscription_cache.active_vm_slot == 0) ? 1 : 0;
    rdx_hogp_subscription_build_record(record, candidate, next_revision);

    if (syscfg_write(rdx_hogp_subscription_vm_id(target_slot), record,
                     sizeof(record)) != sizeof(record) ||
        syscfg_read(rdx_hogp_subscription_vm_id(target_slot), readback,
                    sizeof(readback)) != sizeof(readback) ||
        memcmp(record, readback, sizeof(record)) != 0 ||
        rdx_hogp_subscription_parse_record(readback, &verified)) {
        y_printf("[HOGP_SUB] VM publish failed slot=%u\n", target_slot);
        return -1;
    }

    verified.active_vm_slot = target_slot;
    memcpy(&s_hogp_subscription_cache, &verified,
           sizeof(s_hogp_subscription_cache));
    return 0;
}

int rdx_hogp_subscription_store_contains(const u8 *peer_addr)
{
    u8 i;

    if (!rdx_hogp_subscription_peer_valid(peer_addr)) {
        return 0;
    }
    rdx_hogp_subscription_load();
    for (i = 0; i < RDX_HOGP_SUB_VM_SLOT_COUNT; i++) {
        if (s_hogp_subscription_cache.entries[i].valid &&
            memcmp(s_hogp_subscription_cache.entries[i].peer_addr,
                   peer_addr, RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN) == 0) {
            return 1;
        }
    }
    return 0;
}

int rdx_hogp_subscription_store_set(const u8 *peer_addr, u8 enabled)
{
    rdx_hogp_subscription_cache_t candidate;
    int found = -1;
    int i;

    if (!rdx_hogp_subscription_peer_valid(peer_addr)) {
        return -1;
    }
    rdx_hogp_subscription_load();
    for (i = 0; i < RDX_HOGP_SUB_VM_SLOT_COUNT; i++) {
        if (s_hogp_subscription_cache.entries[i].valid &&
            memcmp(s_hogp_subscription_cache.entries[i].peer_addr,
                   peer_addr, RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN) == 0) {
            found = i;
            break;
        }
    }
    if ((enabled && found == 0) || (!enabled && found < 0)) {
        return 0;
    }

    memcpy(&candidate, &s_hogp_subscription_cache, sizeof(candidate));
    if (found >= 0) {
        for (i = found; i < RDX_HOGP_SUB_VM_SLOT_COUNT - 1; i++) {
            candidate.entries[i] = candidate.entries[i + 1];
        }
        memset(&candidate.entries[RDX_HOGP_SUB_VM_SLOT_COUNT - 1], 0,
               sizeof(candidate.entries[0]));
    }
    if (enabled) {
        for (i = RDX_HOGP_SUB_VM_SLOT_COUNT - 1; i > 0; i--) {
            candidate.entries[i] = candidate.entries[i - 1];
        }
        candidate.entries[0].valid = 1;
        memcpy(candidate.entries[0].peer_addr, peer_addr,
               RDX_HOGP_SUBSCRIPTION_PEER_ADDR_LEN);
    }
    return rdx_hogp_subscription_publish(&candidate);
}

int rdx_hogp_subscription_store_reset(void)
{
    rdx_hogp_subscription_cache_t candidate;

    rdx_hogp_subscription_load();
    memset(&candidate, 0, sizeof(candidate));
    candidate.loaded = 1;
    candidate.active_vm_slot = s_hogp_subscription_cache.active_vm_slot;
    candidate.revision = s_hogp_subscription_cache.revision;
    return rdx_hogp_subscription_publish(&candidate);
}

#else

int rdx_hogp_subscription_store_contains(const u8 *peer_addr)
{
    (void)peer_addr;
    return 0;
}

int rdx_hogp_subscription_store_set(const u8 *peer_addr, u8 enabled)
{
    (void)peer_addr;
    (void)enabled;
    return 0;
}

int rdx_hogp_subscription_store_reset(void)
{
    return 0;
}

#endif
