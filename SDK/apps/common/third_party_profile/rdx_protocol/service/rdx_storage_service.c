#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"
#include "rdx_command_dispatch.h"
#include "rdx_record.h"
#include "rdx_wifi_service.h"

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

static void rdx_storage_on_format_done(rdx_event_id_t event, void *payload, u32 len, void *ctx)
{
	(void)event; (void)payload; (void)len; (void)ctx;
	RDX_LOGI("storage format done event consumed");
}

/* ---- migrated handlers (Stage 4 from rdx_app.c) ---- */

static void rdx_cmd_handle_sd_mem_query(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	ops->sd_mem_indicate(0, 0);
	rdx_uxfile_device_sd_mem_check();
}

static void rdx_cmd_handle_file_delete(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if (!data || len < sizeof(ProtocolFileDeleteParams)) return;
	ProtocolFileDeleteParams *p = (ProtocolFileDeleteParams *)data;
	g_printf("[APP CMD] file_delete sn=%d name=%s\r", p->file_sn, p->file_name);
	int ret = rdx_uxfile_recordFile_delete_handle(p->file_sn, p->file_name);
	ops->file_delete_ack_indicate((ret < 0) ? 1 : 0, p->file_sn, p->file_name);
}

void rdx_storage_service_init(void)
{
	rdx_cmd_register(PROTOCOL_EVENT_CMD_SD_FORMAT, rdx_cmd_handle_sd_format);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_SD_MEM_QUERY, rdx_cmd_handle_sd_mem_query);
	rdx_cmd_register(PROTOCOL_EVENT_CMD_FILE_DELETE, rdx_cmd_handle_file_delete);
	if (rdx_event_subscribe(RDX_EVENT_STORAGE_FORMAT_DONE,
							rdx_storage_on_format_done, NULL) != RDX_OK) {
		RDX_LOGW("storage format event subscribe failed");
	}
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
	rdx_uxfile_sd_format(rdx_storage_service_format_cb);
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
