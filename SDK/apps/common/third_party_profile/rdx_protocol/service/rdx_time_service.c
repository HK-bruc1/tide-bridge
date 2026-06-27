#include "rdx_time_service.h"
#include "rdx_command_dispatch.h"
#include "rdx_event_bus.h"
#include "rdx_protocol.h"
#include "rdx_rtc.h"
#include "rdx_log.h"
#include "system/includes.h"

/*
 * RTC handler — Stage 4 migrated from rdx_app.c.
 * Sets device RTC from protocol command; if recording is active,
 * corrects the uxfile start_time by the time delta.
 */
static void rdx_cmd_handle_rtc(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < sizeof(ProtocolRtcParams)) return;
	ProtocolRtcParams *p = (ProtocolRtcParams *)data;
	if (p->timestamp > 0) {
		time_t old_rtc = rdx_rtc_get();
		int result = rdx_rtc_set_timestamp(p->timestamp);
		if (result == 0 && old_rtc > 0) {
			rdx_time_sync_event_t sync = {
				.timestamp = p->timestamp,
				.delta = (int)((time_t)p->timestamp - old_rtc),
			};
			rdx_event_publish_async(RDX_EVENT_TIME_SYNCED, &sync, sizeof(sync));
		}
		ops->rtc_set_ack_indicate((u8)result, p->timestamp);
	} else {
		ops->rtc_set_ack_indicate(1, p->timestamp);
	}
}

void rdx_time_service_init(void)
{
	rdx_cmd_register(PROTOCOL_EVENT_CMD_RTC, rdx_cmd_handle_rtc);
	RDX_LOGI("time_service init done");
}
