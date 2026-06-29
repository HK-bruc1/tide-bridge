#include "test_minimal.h"
#include "rdx_event_bus.h"
#include "rdx_err.h"

static int g_cb_count = 0;
static rdx_event_id_t g_last_event = 0;
static int g_last_payload = 0;

static void test_callback(rdx_event_id_t event, void *payload, u32 len, void *user_ctx)
{
    g_cb_count++;
    g_last_event = event;
    if (payload && len >= sizeof(int)) {
        g_last_payload = *(int *)payload;
    }
    (void)user_ctx;
}

static int test_subscribe_publish(void)
{
    int payload = 42;

    g_cb_count = 0;
    g_last_payload = 0;
    rdx_event_bus_init();
    TEST_ASSERT(rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED, test_callback, NULL) == RDX_OK);

    rdx_event_publish(RDX_EVENT_BLE_CONNECTED, &payload, sizeof(payload));

    TEST_ASSERT(g_cb_count == 1);
    TEST_ASSERT(g_last_event == RDX_EVENT_BLE_CONNECTED);
    TEST_ASSERT(g_last_payload == 42);

    TEST_PASS();
}

static int test_unsubscribe(void)
{
    g_cb_count = 0;
    rdx_event_bus_init();
    TEST_ASSERT(rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED, test_callback, NULL) == RDX_OK);
    TEST_ASSERT(rdx_event_unsubscribe(RDX_EVENT_BLE_CONNECTED, test_callback, NULL) == RDX_OK);

    rdx_event_publish(RDX_EVENT_BLE_CONNECTED, NULL, 0);

    TEST_ASSERT(g_cb_count == 0);

    TEST_PASS();
}

static int test_async_publish(void)
{
    int payload = 99;

    g_cb_count = 0;
    g_last_payload = 0;
    rdx_event_bus_init();
    TEST_ASSERT(rdx_event_subscribe(RDX_EVENT_TIME_SYNCED, test_callback, NULL) == RDX_OK);

    /*
     * Note: rdx_event_publish_async() stores the callback and a pointer in
     * two 32-bit ints and posts them to a mock task queue. The mock
     * os_taskq_post_type() below executes the callback synchronously, so the
     * pointer remains valid. On a real 64-bit host without the mock, the
     * (int) cast of a pointer would truncate; this test intentionally covers
     * the current JL 32-bit taskq convention only.
     */
    TEST_ASSERT(rdx_event_publish_async(RDX_EVENT_TIME_SYNCED, &payload, sizeof(payload)) == RDX_OK);

    TEST_ASSERT(g_cb_count == 1);
    TEST_ASSERT(g_last_event == RDX_EVENT_TIME_SYNCED);
    TEST_ASSERT(g_last_payload == 99);

    TEST_PASS();
}

static int g_cb2_count = 0;

static void test_callback2(rdx_event_id_t event, void *payload, u32 len, void *user_ctx)
{
    g_cb2_count++;
    (void)event;
    (void)payload;
    (void)len;
    (void)user_ctx;
}

static int test_multiple_subscribers(void)
{
    g_cb_count = 0;
    g_cb2_count = 0;
    rdx_event_bus_init();
    TEST_ASSERT(rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED, test_callback, NULL) == RDX_OK);
    TEST_ASSERT(rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED, test_callback2, NULL) == RDX_OK);

    rdx_event_publish(RDX_EVENT_BLE_CONNECTED, NULL, 0);

    TEST_ASSERT(g_cb_count == 1);
    TEST_ASSERT(g_cb2_count == 1);

    TEST_PASS();
}

/*
 * 8 distinct callbacks for subscribe-full test.
 * The subscriber lookup in rdx_event_bus.c keys off the callback pointer,
 * so we need 8 unique functions to fill a single event's subscriber array.
 */
#define DEF_CB(n) \
    static int g_cb_full##n##_count = 0; \
    static void cb_full##n(rdx_event_id_t e, void *p, u32 l, void *c) { \
        g_cb_full##n##_count++; (void)e; (void)p; (void)l; (void)c; \
    }
DEF_CB(0) DEF_CB(1) DEF_CB(2) DEF_CB(3)
DEF_CB(4) DEF_CB(5) DEF_CB(6) DEF_CB(7)

static int test_subscribe_full(void)
{
    rdx_event_bus_init();
    rdx_event_callback_t cbs[8] = { cb_full0, cb_full1, cb_full2, cb_full3, cb_full4, cb_full5, cb_full6, cb_full7 };
    int i;

    /* fill all 8 subscriber slots */
    for (i = 0; i < 8; i++) {
        TEST_ASSERT(rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED, cbs[i], NULL) == RDX_OK);
    }

    /* 9th subscription on same event must fail */
    int ret = rdx_event_subscribe(RDX_EVENT_BLE_CONNECTED, test_callback, NULL);
    TEST_ASSERT(ret == RDX_ERR_NOMEM);

    TEST_PASS();
}

/*
 * Pool-full test: async publish with a taskq mock that stores events
 * instead of executing them synchronously so we can fill the 4-slot pool
 * and verify RDX_ERR_NOMEM on the 5th post.
 *
 * The default os_taskq_post_type mock executes synchronously and frees
 * the slot inline, so pool-full cannot be triggered without a
 * store-and-replay mock. The JL platform guarantee (4-slot pool, pool-full
 * checked in rdx_event_publish_async) is verified by code review and
 * the boundary check script; this test targets the host-visible return
 * path by testing with a non-executing taskq.
 */
extern int g_os_taskq_store_mode;   /* set non-zero to store instead of exec */
extern int g_os_taskq_store_count;
extern int g_os_taskq_last_arg0;
extern int g_os_taskq_last_arg1;

static int test_async_pool_full(void)
{
    int payload = 1;
    int i, ret;

    g_os_taskq_store_mode = 1;
    g_os_taskq_store_count = 0;
    rdx_event_bus_init();
    rdx_event_subscribe(RDX_EVENT_TIME_SYNCED, test_callback, NULL);

    /* fill all 4 pool slots */
    for (i = 0; i < 4; i++) {
        ret = rdx_event_publish_async(RDX_EVENT_TIME_SYNCED, &payload, sizeof(payload));
        TEST_ASSERT(ret == RDX_OK);
    }
    TEST_ASSERT(g_os_taskq_store_count == 4);

    /* 5th post must return RDX_ERR_NOMEM */
    ret = rdx_event_publish_async(RDX_EVENT_TIME_SYNCED, &payload, sizeof(payload));
    TEST_ASSERT(ret == RDX_ERR_NOMEM);

    g_os_taskq_store_mode = 0;
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_subscribe_publish);
    TEST_RUN(test_unsubscribe);
    TEST_RUN(test_async_publish);
    TEST_RUN(test_multiple_subscribers);
    TEST_RUN(test_subscribe_full);
    TEST_RUN(test_async_pool_full);

    printf("All event bus tests passed.\n");
    return 0;
}
