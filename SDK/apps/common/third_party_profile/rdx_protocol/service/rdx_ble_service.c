#include "rdx_ble_service.h"
#include "rdx_record_service.h"
#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"

/*
 * Stage 5 BLE boundary cutover complete.
 * stop_recording / cleanup_protocol_state delegate to record_service
 * and storage_service APIs; BLE layer no longer accesses RecordStatus,
 * rdx_record_*, rdx_protocol_*, or rdx_uxfile_* directly.
 */
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

rdx_err_t rdx_ble_service_stop_recording(void)
{
	return rdx_record_service_stop_from_ble();
}

rdx_err_t rdx_ble_service_cleanup_protocol_state(void)
{
	return rdx_storage_service_cleanup_ble_buffers();
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
