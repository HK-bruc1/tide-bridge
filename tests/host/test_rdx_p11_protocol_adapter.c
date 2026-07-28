#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c"

typedef struct {
    void (*callback)(void *, void *);
    void *arg1;
    void *arg2;
} queued_callback_t;

static queued_callback_t g_callbacks[8];
static unsigned g_callback_count;
static unsigned g_indicate_count;
static unsigned g_critical_depth;
static bool g_fail_post;
static RecordStatus g_last_indicate;
static u8 g_last_factor;

void p11_test_critical_enter(void)
{
    assert(g_critical_depth == 0);
    g_critical_depth++;
}

void p11_test_critical_exit(void)
{
    assert(g_critical_depth == 1);
    g_critical_depth--;
}

rdx_err_t rdx_os_task_post_callback2(const char *task_name,
                                     void (*callback)(void *, void *),
                                     void *arg1,
                                     void *arg2)
{
    assert(strcmp(task_name, "app_core") == 0);
    if (g_fail_post) {
        return RDX_ERR_IO;
    }
    assert(g_callback_count < 8);
    g_callbacks[g_callback_count].callback = callback;
    g_callbacks[g_callback_count].arg1 = arg1;
    g_callbacks[g_callback_count].arg2 = arg2;
    g_callback_count++;
    return RDX_OK;
}

void rdx_protocol_record_trigger_indicate(RecordStatus *rp, u8 factor)
{
    g_last_indicate = *rp;
    g_last_factor = factor;
    g_indicate_count++;
}

static void run_callback(unsigned index)
{
    g_callbacks[index].callback(g_callbacks[index].arg1,
                                g_callbacks[index].arg2);
}

static rdx_record_trigger_payload_t payload_for(
    rdx_record_trigger_payload_kind_t kind,
    u8 value)
{
    rdx_record_trigger_payload_t payload;

    payload.kind = kind;
    payload.run = value;
    payload.format = (u8)(value + 1);
    payload.scene = (u8)(value + 2);
    payload.mode = (u8)(value + 3);
    payload.factor = (u8)(value + 4);
    return payload;
}

static void test_pool_pressure_and_callback_lifetime(void)
{
    rdx_record_trigger_payload_t payload;
    unsigned i;

    rdx_record_protocol_adapter_init();
    g_callback_count = 0;
    g_indicate_count = 0;
    for (i = 0; i < 4; i++) {
        payload = payload_for(RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD, (u8)(10 + i));
        assert(rdx_record_protocol_post_trigger(&payload) == RDX_OK);
    }
    payload = payload_for(RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD, 20);
    assert(rdx_record_protocol_post_trigger(&payload) == RDX_ERR_NOMEM);
    assert(g_callback_count == 4);

    run_callback(0);
    assert(g_indicate_count == 1);
    assert(g_last_indicate.run == 10);
    assert(g_last_indicate.formate == 11);
    assert(g_last_indicate.scene == 12);
    assert(g_last_factor == 14);

    assert(rdx_record_protocol_post_trigger(&payload) == RDX_OK);
    assert(g_callback_count == 5);
    assert(g_critical_depth == 0);
}

static void test_kind_specific_fill(void)
{
    rdx_record_protocol_reservation_t reservation;
    rdx_record_trigger_payload_t payload;

    rdx_record_protocol_adapter_init();
    g_record_protocol_pool[0].orig_scene = 77;
    g_record_protocol_pool[0].mode = 88;
    assert(rdx_record_protocol_reserve(
               RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD, &reservation) == RDX_OK);
    payload = payload_for(RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD, 1);
    assert(rdx_record_protocol_fill_reserved(&reservation, &payload) == RDX_OK);
    assert(g_record_protocol_pool[0].orig_scene == 77);
    assert(g_record_protocol_pool[0].mode == 88);
    rdx_record_protocol_cancel_reserved(&reservation);

    assert(rdx_record_protocol_reserve(
               RDX_RECORD_TRIGGER_PAYLOAD_DEVICE, &reservation) == RDX_OK);
    assert(g_record_protocol_pool[0].orig_scene == 0);
    assert(g_record_protocol_pool[0].mode == 0);
    payload = payload_for(RDX_RECORD_TRIGGER_PAYLOAD_DEVICE, 2);
    assert(rdx_record_protocol_fill_reserved(&reservation, &payload) == RDX_OK);
    assert(g_record_protocol_pool[0].mode == 5);
    rdx_record_protocol_cancel_reserved(&reservation);
}

static void test_post_failure_releases_slot(void)
{
    rdx_record_protocol_reservation_t reservation;
    rdx_record_trigger_payload_t payload =
        payload_for(RDX_RECORD_TRIGGER_PAYLOAD_SWITCH, 3);

    rdx_record_protocol_adapter_init();
    assert(rdx_record_protocol_reserve(payload.kind, &reservation) == RDX_OK);
    assert(rdx_record_protocol_fill_reserved(&reservation, &payload) == RDX_OK);
    g_fail_post = true;
    assert(rdx_record_protocol_post_reserved(&reservation) == RDX_ERR_IO);
    g_fail_post = false;
    assert(!reservation.active);
    assert(rdx_record_protocol_reserve(payload.kind, &reservation) == RDX_OK);
    assert(reservation.slot == 0);
    rdx_record_protocol_cancel_reserved(&reservation);
}

int main(void)
{
    test_pool_pressure_and_callback_lifetime();
    test_kind_specific_fill();
    test_post_failure_releases_slot();
    puts("P11 protocol adapter host tests passed.");
    return 0;
}
