#include "test_minimal.h"
#include "rdx_command_dispatch.h"
#include "rdx_event_bus.h"
#include "rdx_time_service.h"
#include "rdx_protocol.h"
#include "rdx_err.h"

/* ---- mocks for rdx_time_service_init dependencies ---- */

static u8  g_mock_ack_result;
static u32 g_mock_ack_timestamp;
static int g_mock_ack_count;

static void mock_rtc_set_ack_indicate(u8 result, u32 timestamp)
{
	g_mock_ack_result    = result;
	g_mock_ack_timestamp  = timestamp;
	g_mock_ack_count++;
}

static RdxProtocolIndicateOps g_mock_ops = {
	.rtc_set_ack_indicate = mock_rtc_set_ack_indicate,
};

const RdxProtocolIndicateOps *rdx_protocol_get_indicate_ops(void)
{
	return &g_mock_ops;
}

static u32 g_mock_rtc = 0;

time_t rdx_rtc_get(void)
{
	return (time_t)g_mock_rtc;
}

static int g_mock_rtc_set_result = 0;

int rdx_rtc_set_timestamp(time_t timestamp)
{
	(void)timestamp;
	return g_mock_rtc_set_result;
}

static void reset_mocks(void)
{
	g_mock_ack_result    = 0xFF;
	g_mock_ack_timestamp  = 0;
	g_mock_ack_count      = 0;
	g_mock_rtc            = 0;
	g_mock_rtc_set_result = 0;
}

/* ---- test cases ---- */

static int test_init_registers_handler(void)
{
	reset_mocks();
	rdx_cmd_dispatch_init();
	rdx_event_bus_init();

	rdx_time_service_init();

	/* dispatch RTC command with valid timestamp, old_rtc > 0 */
	g_mock_rtc = 1000000;
	ProtocolRtcParams params = { .timestamp = 2000000 };
	rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RTC, &params, sizeof(params));

	TEST_ASSERT(g_mock_ack_count == 1);
	TEST_ASSERT(g_mock_ack_result == 0);
	TEST_ASSERT(g_mock_ack_timestamp == 2000000);

	TEST_PASS();
}

static int test_init_rtc_zero_timestamp(void)
{
	reset_mocks();
	rdx_cmd_dispatch_init();
	rdx_event_bus_init();

	rdx_time_service_init();

	/* timestamp == 0 → early exit with result=1 */
	g_mock_rtc = 1000000;
	ProtocolRtcParams params = { .timestamp = 0 };
	rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RTC, &params, sizeof(params));

	TEST_ASSERT(g_mock_ack_count == 1);
	TEST_ASSERT(g_mock_ack_result == 1);

	TEST_PASS();
}

static int test_init_old_rtc_zero(void)
{
	reset_mocks();
	rdx_cmd_dispatch_init();
	rdx_event_bus_init();

	rdx_time_service_init();

	/* old_rtc == 0 → result=1 (delta calc skipped) */
	g_mock_rtc = 0;
	ProtocolRtcParams params = { .timestamp = 2000000 };
	rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RTC, &params, sizeof(params));

	TEST_ASSERT(g_mock_ack_count == 1);
	TEST_ASSERT(g_mock_ack_result == 0);
	TEST_ASSERT(g_mock_ack_timestamp == 2000000);

	TEST_PASS();
}

static int test_init_null_data(void)
{
	reset_mocks();
	rdx_cmd_dispatch_init();
	rdx_event_bus_init();

	rdx_time_service_init();

	/* NULL data → handler returns early, no crash, no ack */
	rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RTC, NULL, 0);

	TEST_ASSERT(g_mock_ack_count == 0);

	TEST_PASS();
}

static int test_init_short_data(void)
{
	reset_mocks();
	rdx_cmd_dispatch_init();
	rdx_event_bus_init();

	rdx_time_service_init();

	/* data shorter than sizeof(ProtocolRtcParams) → handler returns early */
	u8 dummy = 0;
	rdx_cmd_dispatch(PROTOCOL_EVENT_CMD_RTC, &dummy, 1);

	TEST_ASSERT(g_mock_ack_count == 0);

	TEST_PASS();
}

int main(void)
{
	TEST_RUN(test_init_registers_handler);
	TEST_RUN(test_init_rtc_zero_timestamp);
	TEST_RUN(test_init_old_rtc_zero);
	TEST_RUN(test_init_null_data);
	TEST_RUN(test_init_short_data);

	printf("All time_service tests passed.\n");
	return 0;
}
