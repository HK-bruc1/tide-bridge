#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"
#include "rdx_command_dispatch.h"
#include "rdx_record_service.h"
#include "rdx_wifi_service.h"

extern u8 get_ota_status(void);

static void rdx_cmd_handle_sd_format(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(get_ota_status() ||
	   rdx_record_service_is_running() ||
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

rdx_err_t rdx_storage_format_request(void)
{
	return RDX_ERR_NOTSUP;
}

rdx_err_t rdx_storage_sync_start(void)
{
	return RDX_ERR_NOTSUP;
}

rdx_err_t rdx_storage_sync_stop(void)
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

/* BLE cleanup — Stage 5: moved from rdx_ble_service.c to break cross-layer coupling */
extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_send_buffer_reinit(void);

rdx_err_t rdx_storage_service_cleanup_ble_immediate(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
	return RDX_OK;
}

rdx_err_t rdx_storage_service_cleanup_ble_buffers(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_uxfile_datFileInfo_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
	return RDX_OK;
}

rdx_err_t rdx_storage_service_adjust_active_record_time(int delta_seconds)
{
	uxfile_data_t *op = rdx_uxfile_get_operateFile_info();

	if (op && op->start_time > 0) {
		u32 corrected = (u32)((int)op->start_time + delta_seconds);
		y_printf("[RTC_SYNC] Recording active, fix start_time: %u -> %u (delta=%d)\r",
		         op->start_time, corrected, delta_seconds);
		op->start_time = corrected;
	}
	return RDX_OK;
}

/* board SD power — Stage 5 D-class from rdx_app.c */
extern void sd_set_power(u8 enable);

rdx_err_t rdx_storage_service_sdmmc_set_power(u8 enable)
{
	sd_set_power(enable);
	if (enable) {
		printf("SD card power on\n");
	} else {
		printf("SD card power off\n");
	}
	return RDX_OK;
}
