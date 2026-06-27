#include "rdx_ble_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_record.h"

/*
 * Stage 4 BLE event bus cutover complete.
 * Record/protocol actions fire through rdx_record_service event subscriber.
 * Remaining externs serve rdx_ble_service_stop_recording / cleanup_protocol_state.
 */
extern void rdx_record_process(void);
extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_send_buffer_reinit(void);
extern void rdx_uxfile_recordFileData_sendBuf_free(void);
extern void rdx_uxfile_datFileInfo_sendBuf_free(void);

static u8 g_ble_connected = 0;

void rdx_ble_service_init(void)
{
	g_ble_connected = 0;
	RDX_LOGI("ble_service init done");
}

void rdx_ble_service_on_connected(void)
{
	g_ble_connected = 1;
	rdx_event_publish_async(RDX_EVENT_BLE_CONNECTED, NULL, 0);
}

void rdx_ble_service_on_disconnected(void)
{
	g_ble_connected = 0;
	rdx_event_publish_async(RDX_EVENT_BLE_DISCONNECTED, NULL, 0);
}

void rdx_ble_service_stop_recording(void)
{
	RecordStatus *rp = rdx_record_get_status();
	if (rp && (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)) {
		rp->run = RECORD_STATE_STOP;
		rdx_record_process();
	}
}

void rdx_ble_service_cleanup_protocol_state(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_uxfile_datFileInfo_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
}

int rdx_ble_send_data(const u8 *data, u32 len)
{
	(void)data;
	(void)len;
	return 0;
}

u8 rdx_ble_is_connected(void)
{
	return g_ble_connected;
}

u16 rdx_ble_get_mtu(void)
{
	return 0;
}
