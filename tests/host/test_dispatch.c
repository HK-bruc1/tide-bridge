#include "test_minimal.h"
#include "rdx_command_dispatch.h"

static int g_called = 0;
static ProtocolEvents g_last_event = 0;

static void test_handler(ProtocolEvents event, void *data, u32 len)
{
    g_called++;
    g_last_event = event;
    (void)data;
    (void)len;
}

static int test_init_clears_table(void)
{
    g_called = 0;
    rdx_cmd_dispatch_init();

    /* Dispatch an unregistered event should not call handler. */
    rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RECORD, NULL, 0);
    TEST_ASSERT(g_called == 0);

    TEST_PASS();
}

static int test_register_and_dispatch(void)
{
    g_called = 0;
    rdx_cmd_dispatch_init();

    rdx_cmd_register(PROTOCOL_EVENT_CMD_RECORD, test_handler);
    rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RECORD, NULL, 0);

    TEST_ASSERT(g_called == 1);
    TEST_ASSERT(g_last_event == PROTOCOL_EVENT_CMD_RECORD);

    TEST_PASS();
}

static int test_unregistered_event_warns(void)
{
    g_called = 0;
    rdx_cmd_dispatch_init();

    /* No handler registered; should not crash and should not call handler. */
    rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RTC, NULL, 0);
    TEST_ASSERT(g_called == 0);

    TEST_PASS();
}

static int test_invalid_event_ignored(void)
{
    g_called = 0;
    rdx_cmd_dispatch_init();

    rdx_cmd_register(PROTOCOL_EVENT_CMD_RECORD, test_handler);
    rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_TYPE_MAX, NULL, 0);

    TEST_ASSERT(g_called == 0);

    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_init_clears_table);
    TEST_RUN(test_register_and_dispatch);
    TEST_RUN(test_unregistered_event_warns);
    TEST_RUN(test_invalid_event_ignored);

    printf("All command dispatch tests passed.\n");
    return 0;
}
