#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"

/*
 * Phase 2 shell: formatting/sync orchestration will be extracted from
 * rdx_app.c (rdx_app_format_handle/cb) after WiFi transport callbacks
 * are verified stable.
 */

void rdx_storage_service_init(void)
{
	RDX_LOGI("storage_service init done");
}

int rdx_storage_format_request(void)
{
	return RDX_ERR_NOTSUP;
}

int rdx_storage_sync_start(void)
{
	return RDX_ERR_NOTSUP;
}

int rdx_storage_sync_stop(void)
{
	return RDX_ERR_NOTSUP;
}

u8 rdx_storage_is_syncing(void)
{
	return 0;
}
