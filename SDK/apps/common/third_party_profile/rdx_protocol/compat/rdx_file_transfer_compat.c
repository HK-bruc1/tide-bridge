#include "rdx_file_transfer_compat.h"
#include "system/includes.h"
#include "rdx_protocol.h"
#include "rdx_uxfile.h"
#include "rdx_jl_osal.h"

extern void rdx_protocol_uploadFileInfo_clean(void);
extern void rdx_protocol_file_sync_busy_timer_stop(void);
extern void rdx_protocol_prepared_data_clean(void);
extern void rdx_protocol_send_buffer_reinit(void);

static int rdx_file_transfer_compat_post_owner(ReqFileInfo *info)
{
    return os_taskq_post_msg(RDX_PROTOCOL_SEND_TASK_NAME, 1, info);
}

rdx_err_t rdx_file_transfer_compat_get_status(
    rdx_file_transfer_compat_status_t *out)
{
    ReqFileInfo *info;

    if (!out) {
        return RDX_ERR_INVAL;
    }

    info = rdx_protocol_get_uploadfileInfo();
    out->available = info != NULL;
    out->busy = info && info->file_send_busy == true;
    out->stopped = info && info->send_stop == true;
    return RDX_OK;
}

rdx_err_t rdx_file_transfer_compat_cleanup_record_disconnect(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
	return RDX_OK;
}

rdx_err_t rdx_file_transfer_compat_cleanup_ble_delayed(void)
{
	rdx_protocol_uploadFileInfo_clean();
	rdx_uxfile_recordFileData_sendBuf_free();
	rdx_uxfile_datFileInfo_sendBuf_free();
	rdx_protocol_file_sync_busy_timer_stop();
	rdx_protocol_send_buffer_reinit();
	return RDX_OK;
}

void rdx_file_transfer_compat_on_tx_done(
    rdx_file_transfer_timer_control_t timer_control,
    const void *timer_ctx)
{
    ReqFileInfo *info = rdx_protocol_get_uploadfileInfo();

    if (!info) {
        return;
    }

    if (info->file_send_busy == true) {
        info->file_send_busy = false;
    }

    if (info->send_stop == true) {
        rdx_protocol_file_sync_busy_timer_stop();
        rdx_uxfile_recordFileData_sendBuf_free();
        rdx_protocol_prepared_data_clean();
        if (timer_control) {
            timer_control(RDX_FILE_TRANSFER_TIMER_STOP, timer_ctx);
            timer_control(RDX_FILE_TRANSFER_TIMER_START, timer_ctx);
        }
        info->interrupt = false;
        return;
    }

    if (info->interrupt == true) {
        info->interrupt = false;
        if (info->loop == true) {
            rdx_file_transfer_compat_post_owner(info);
        }
        return;
    }

    if (info->loop == true) {
        int post_result;

        if (info->total_pack > info->pack_num) {
            info->pack_num++;
            info->ack = 0;
        } else {
            info->pack_num = 0;
            info->ack = 0;
        }
        rdx_os_time_dly(2);
        post_result = rdx_file_transfer_compat_post_owner(info);
        if (post_result != OS_NO_ERR) {
            rdx_os_time_dly(1);
            rdx_file_transfer_compat_post_owner(info);
        }
    }
}

void rdx_file_transfer_compat_retry_on_stuck(void)
{
    ReqFileInfo *info = rdx_protocol_get_uploadfileInfo();

    if (info && info->loop && !info->send_stop && !info->interrupt) {
        info->ack = 1;
        rdx_file_transfer_compat_post_owner(info);
    }
}
