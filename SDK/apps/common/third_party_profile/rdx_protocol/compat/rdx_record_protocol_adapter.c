#include "rdx_record_protocol_adapter.h"
#include <stdint.h>
#include "system/includes.h"
#include "rdx_record.h"
#include "rdx_jl_osal.h"

#define RDX_RECORD_PROTOCOL_POOL_SIZE 4

static RecordStatus g_record_protocol_pool[RDX_RECORD_PROTOCOL_POOL_SIZE];
static u8 g_record_protocol_busy[RDX_RECORD_PROTOCOL_POOL_SIZE];

extern void rdx_protocol_record_trigger_indicate(RecordStatus *rp, u8 factor);

static bool rdx_record_protocol_kind_valid(
    rdx_record_trigger_payload_kind_t kind)
{
    return kind == RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD ||
           kind == RDX_RECORD_TRIGGER_PAYLOAD_DEVICE ||
           kind == RDX_RECORD_TRIGGER_PAYLOAD_SWITCH;
}

static RecordStatus *rdx_record_protocol_pool_alloc(u8 *index)
{
    u8 i;

    CPU_CRITICAL_ENTER();
    for (i = 0; i < RDX_RECORD_PROTOCOL_POOL_SIZE; i++) {
        if (!g_record_protocol_busy[i]) {
            g_record_protocol_busy[i] = 1;
            CPU_CRITICAL_EXIT();
            *index = i;
            return &g_record_protocol_pool[i];
        }
    }
    CPU_CRITICAL_EXIT();
    return NULL;
}

static void rdx_record_protocol_pool_release(RecordStatus *slot)
{
    u8 index = (u8)(slot - g_record_protocol_pool);

    CPU_CRITICAL_ENTER();
    g_record_protocol_busy[index] = 0;
    CPU_CRITICAL_EXIT();
}

static void rdx_record_protocol_pool_callback(void *p1, void *p2)
{
    RecordStatus *slot = (RecordStatus *)p1;
    u8 factor = (u8)(uintptr_t)p2;

    rdx_protocol_record_trigger_indicate(slot, factor);
    rdx_record_protocol_pool_release(slot);
}

void rdx_record_protocol_adapter_init(void)
{
    memset(g_record_protocol_busy, 0, sizeof(g_record_protocol_busy));
    memset(g_record_protocol_pool, 0, sizeof(g_record_protocol_pool));
}

rdx_err_t rdx_record_protocol_reserve(
    rdx_record_trigger_payload_kind_t kind,
    rdx_record_protocol_reservation_t *reservation)
{
    RecordStatus *slot;
    u8 index;

    if (!reservation || !rdx_record_protocol_kind_valid(kind)) {
        return RDX_ERR_INVAL;
    }
    reservation->active = false;
    reservation->filled = false;
    slot = rdx_record_protocol_pool_alloc(&index);
    if (!slot) {
        return RDX_ERR_NOMEM;
    }
    if (kind == RDX_RECORD_TRIGGER_PAYLOAD_DEVICE) {
        memset(slot, 0, sizeof(*slot));
    }
    reservation->slot = index;
    reservation->kind = kind;
    reservation->active = true;
    return RDX_OK;
}

rdx_err_t rdx_record_protocol_fill_reserved(
    rdx_record_protocol_reservation_t *reservation,
    const rdx_record_trigger_payload_t *payload)
{
    RecordStatus *slot;

    if (!reservation || !reservation->active || !payload ||
        reservation->slot >= RDX_RECORD_PROTOCOL_POOL_SIZE ||
        reservation->kind != payload->kind) {
        return RDX_ERR_INVAL;
    }
    slot = &g_record_protocol_pool[reservation->slot];
    slot->run = payload->run;
    slot->formate = payload->format;
    slot->scene = payload->scene;
    reservation->factor = payload->factor;
    if (payload->kind == RDX_RECORD_TRIGGER_PAYLOAD_DEVICE) {
        slot->mode = payload->mode;
    }
    reservation->filled = true;
    return RDX_OK;
}

rdx_err_t rdx_record_protocol_post_reserved(
    rdx_record_protocol_reservation_t *reservation)
{
    RecordStatus *slot;
    rdx_err_t ret;

    if (!reservation || !reservation->active || !reservation->filled ||
        reservation->slot >= RDX_RECORD_PROTOCOL_POOL_SIZE) {
        return RDX_ERR_INVAL;
    }
    slot = &g_record_protocol_pool[reservation->slot];
    ret = rdx_os_task_post_callback2(
        "app_core",
        rdx_record_protocol_pool_callback,
        slot,
        (void *)(uintptr_t)reservation->factor);
    if (ret != RDX_OK) {
        rdx_record_protocol_pool_release(slot);
        reservation->active = false;
        return RDX_ERR_IO;
    }
    reservation->active = false;
    return RDX_OK;
}

void rdx_record_protocol_cancel_reserved(
    rdx_record_protocol_reservation_t *reservation)
{
    if (!reservation || !reservation->active ||
        reservation->slot >= RDX_RECORD_PROTOCOL_POOL_SIZE) {
        return;
    }
    rdx_record_protocol_pool_release(
        &g_record_protocol_pool[reservation->slot]);
    reservation->active = false;
}

rdx_err_t rdx_record_protocol_post_trigger(
    const rdx_record_trigger_payload_t *payload)
{
    rdx_record_protocol_reservation_t reservation;
    rdx_err_t ret;

    if (!payload) {
        return RDX_ERR_INVAL;
    }
    ret = rdx_record_protocol_reserve(payload->kind, &reservation);
    if (ret != RDX_OK) {
        return ret;
    }
    ret = rdx_record_protocol_fill_reserved(&reservation, payload);
    if (ret != RDX_OK) {
        rdx_record_protocol_cancel_reserved(&reservation);
        return ret;
    }
    return rdx_record_protocol_post_reserved(&reservation);
}
