#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"
#include "rdx_command_dispatch.h"
#include "rdx_record.h"
#include "rdx_wifi_service.h"

extern bool rdx_uxfile_sd_format_status_check(void);
extern u8 get_ota_status(void);

static void rdx_cmd_handle_sd_format(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	RecordStatus* rp = rdx_record_get_status();
	if(get_ota_status() ||
	   rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
	   rdx_wifi_service_is_file_send_busy()){
	    y_printf("[APP CMD] sd_format rejected: busy\r");
	    ops->sd_format_ack_indicate(1);
	    return;
	}
	ops->sd_format_ack_indicate(0);
	rdx_storage_service_format_handle();

}

void rdx_storage_service_init(void)
{
	rdx_cmd_register(PROTOCOL_EVENT_CMD_SD_FORMAT, rdx_cmd_handle_sd_format);
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
