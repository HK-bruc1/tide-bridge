#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"

extern bool rdx_uxfile_sd_format_status_check(void);

void rdx_storage_service_init(void)
{
	RDX_LOGI("storage_service init done");
}

/* ---- format (moved from rdx_app.c) ---- */

void rdx_storage_service_format_cb(u8 result)
{
	if (result == MEM_FORMAT_RESULT_OK)
		rdx_event_publish(RDX_EVENT_STORAGE_FORMAT_DONE, NULL, 0);
}

void rdx_storage_service_format_handle(void)
{
	rdx_uxfile_sd_format(NULL);
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

u8 rdx_storage_is_formatting(void)
{
	return rdx_uxfile_sd_format_status_check();
}
