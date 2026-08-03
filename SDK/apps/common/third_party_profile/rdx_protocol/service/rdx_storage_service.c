#include "rdx_storage_service.h"
#include "rdx_event_bus.h"
#include "system/includes.h"
#include "rdx_log.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"
#include "rdx_command_dispatch.h"
#include "rdx_record_service.h"
#include "rdx_file_transfer_service.h"
#include "../compat/rdx_file_transfer_cleanup_compat.h"
#include "../internal/rdx_storage_domain.h"

extern u8 get_ota_status(void);

static bool rdx_storage_service_is_file_send_busy(void)
{
    rdx_file_transfer_state_t state = RDX_FILE_TRANSFER_STATE_UNAVAILABLE;

    return rdx_file_transfer_get_state(&state) == RDX_OK &&
           state == RDX_FILE_TRANSFER_STATE_BUSY;
}

static void rdx_cmd_handle_sd_format(ProtocolEvents event, void *data, u32 len)
{
	(void)event; (void)data; (void)len;
	const RdxProtocolIndicateOps *ops = rdx_protocol_get_indicate_ops();
	if (!ops) return;
	if(get_ota_status() ||
	   rdx_record_service_is_running() ||
	   rdx_storage_service_is_file_send_busy()){
	    y_printf("[APP CMD] sd_format rejected: busy\r");
	    ops->sd_format_ack_indicate(1);
	    return;
	}
	ops->sd_format_ack_indicate(0);
	(void)rdx_storage_service_format_for_app();

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

rdx_err_t rdx_storage_service_runtime_init(void)
{
	rdx_uxfile_init();
	return RDX_OK;
}

/* ---- format (moved from rdx_app.c) ---- */

void rdx_storage_service_format_cb(u8 result)
{
	if (result == MEM_FORMAT_RESULT_OK)
		rdx_event_publish(RDX_EVENT_STORAGE_FORMAT_DONE, NULL, 0);
}

rdx_err_t rdx_storage_service_format_for_app(void)
{
	return rdx_uxfile_sd_format(rdx_storage_service_format_cb) == 0
	       ? RDX_OK : RDX_ERR_IO;
}

void rdx_storage_service_format_handle(void)
{
	(void)rdx_storage_service_format_for_app();
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

u8 rdx_storage_is_dat_sync_in_progress(void)
{
	return rdx_uxfile_is_sync_in_progress();
}

u8 rdx_storage_is_file_info_loading(void)
{
	return rdx_uxfile_is_datFileInfo_loading();
}

u8 rdx_storage_is_scan_active(void)
{
	return rdx_uxfile_is_scan_active();
}

u8 rdx_storage_is_format_operation_active(void)
{
	return rdx_uxfile_is_formatting();
}

/* Frozen compatibility facade for the delayed/full BLE cleanup profile. */
rdx_err_t rdx_storage_service_cleanup_ble_buffers(void)
{
	return rdx_file_transfer_compat_cleanup_ble_delayed();
}

rdx_err_t rdx_storage_service_cleanup_ble_immediate(void)
{
	return rdx_file_transfer_compat_cleanup_record_disconnect();
}

rdx_err_t rdx_storage_service_adjust_active_record_time(int delta_seconds)
{
	return rdx_storage_domain_adjust_active_record_time(delta_seconds);
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
