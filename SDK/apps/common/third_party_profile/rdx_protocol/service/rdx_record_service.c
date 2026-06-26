#include "rdx_record_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"

/*
 * Phase 2 shell: recording state machine will be extracted from
 * rdx_app.c (rdx_app_device_record_handle, rdx_app_record_switch,
 * rdx_app_record_state_upload_timer_*) after BLE service adapter
 * is verified stable.
 */

void rdx_record_service_init(void)
{
	RDX_LOGI("record_service init done");
}

void rdx_record_service_exit(void)
{
}

void rdx_record_service_start(u8 mode)
{
	(void)mode;
}

void rdx_record_service_stop(void)
{
}

u8 rdx_record_service_get_state(void)
{
	return 0;
}

u8 rdx_record_service_is_active(void)
{
	return 0;
}
