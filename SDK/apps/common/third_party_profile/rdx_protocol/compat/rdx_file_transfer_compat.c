#include "rdx_file_transfer_compat.h"
#include <stddef.h>
#include "system/includes.h"
#include "rdx_protocol.h"
#include "rdx_uxfile.h"
#include "rdx_jl_osal.h"

#define RDX_REQ_FILE_INFO_ABI_ASSERT(name, condition) \
    typedef char rdx_req_file_info_abi_##name[(condition) ? 1 : -1]

RDX_REQ_FILE_INFO_ABI_ASSERT(size, sizeof(ReqFileInfo) == 52);
RDX_REQ_FILE_INFO_ABI_ASSERT(alignment, __alignof__(ReqFileInfo) == 4);
RDX_REQ_FILE_INFO_ABI_ASSERT(ack, offsetof(ReqFileInfo, ack) == 0);
RDX_REQ_FILE_INFO_ABI_ASSERT(file_num, offsetof(ReqFileInfo, file_num) == 4);
RDX_REQ_FILE_INFO_ABI_ASSERT(is_first_pack, offsetof(ReqFileInfo, is_first_pack) == 8);
RDX_REQ_FILE_INFO_ABI_ASSERT(pack_num, offsetof(ReqFileInfo, pack_num) == 12);
RDX_REQ_FILE_INFO_ABI_ASSERT(orig_pack_num, offsetof(ReqFileInfo, orig_pack_num) == 16);
RDX_REQ_FILE_INFO_ABI_ASSERT(sent_size, offsetof(ReqFileInfo, sent_size) == 20);
RDX_REQ_FILE_INFO_ABI_ASSERT(auto_del, offsetof(ReqFileInfo, auto_del) == 24);
RDX_REQ_FILE_INFO_ABI_ASSERT(file_offset, offsetof(ReqFileInfo, file_offset) == 28);
RDX_REQ_FILE_INFO_ABI_ASSERT(total_pack, offsetof(ReqFileInfo, total_pack) == 32);
RDX_REQ_FILE_INFO_ABI_ASSERT(chunk, offsetof(ReqFileInfo, chunk) == 36);
RDX_REQ_FILE_INFO_ABI_ASSERT(block_cnt, offsetof(ReqFileInfo, block_cnt) == 40);
RDX_REQ_FILE_INFO_ABI_ASSERT(file_send_busy, offsetof(ReqFileInfo, file_send_busy) == 44);
RDX_REQ_FILE_INFO_ABI_ASSERT(loop, offsetof(ReqFileInfo, loop) == 45);
RDX_REQ_FILE_INFO_ABI_ASSERT(interrupt, offsetof(ReqFileInfo, interrupt) == 46);
RDX_REQ_FILE_INFO_ABI_ASSERT(send_stop, offsetof(ReqFileInfo, send_stop) == 47);
RDX_REQ_FILE_INFO_ABI_ASSERT(ble_upload_cancel, offsetof(ReqFileInfo, ble_upload_cancel) == 48);

#undef RDX_REQ_FILE_INFO_ABI_ASSERT

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

rdx_err_t rdx_file_transfer_compat_get_active(int *out)
{
    if (!out) {
        return RDX_ERR_INVAL;
    }

    *out = rdx_is_file_transfer_active();
    return RDX_OK;
}

rdx_err_t rdx_file_transfer_compat_get_sync_busy(int *out)
{
    if (!out) {
        return RDX_ERR_INVAL;
    }

    *out = rdx_is_file_sync_busy();
    return RDX_OK;
}

void rdx_file_transfer_compat_on_ble_connected(void)
{
    rdx_protocol_send_buffer_reinit();
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
