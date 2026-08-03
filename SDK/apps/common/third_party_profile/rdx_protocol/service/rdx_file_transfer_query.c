#include "rdx_file_transfer_service.h"
#include "rdx_uxfile.h"

rdx_err_t rdx_file_transfer_get_state(rdx_file_transfer_state_t *out)
{
    ReqFileInfo *info;

    if (!out) {
        return RDX_ERR_INVAL;
    }

    info = rdx_protocol_get_uploadfileInfo();
    if (!info) {
        *out = RDX_FILE_TRANSFER_STATE_UNAVAILABLE;
        return RDX_OK;
    }

    *out = info->file_send_busy == true
         ? RDX_FILE_TRANSFER_STATE_BUSY
         : RDX_FILE_TRANSFER_STATE_IDLE;
    return RDX_OK;
}
