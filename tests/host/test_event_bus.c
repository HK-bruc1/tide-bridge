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

int main(void)
{
    TEST_RUN(test_subscribe_publish);
    TEST_RUN(test_unsubscribe);
    TEST_RUN(test_async_publish);
    TEST_RUN(test_multiple_subscribers);

    printf("All event bus tests passed.\n");
    return 0;
}
