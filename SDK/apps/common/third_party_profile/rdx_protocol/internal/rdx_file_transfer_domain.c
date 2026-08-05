#include "rdx_file_transfer_domain.h"
#include "../compat/rdx_file_transfer_compat.h"

rdx_err_t rdx_file_transfer_domain_get_state(rdx_file_transfer_state_t *out)
{
    rdx_file_transfer_compat_status_t status;
    rdx_err_t ret;

    if (!out) {
        return RDX_ERR_INVAL;
    }

    ret = rdx_file_transfer_compat_get_status(&status);
    if (ret != RDX_OK) {
        return ret;
    }
    if (!status.available) {
        *out = RDX_FILE_TRANSFER_STATE_UNAVAILABLE;
    } else if (status.busy) {
        *out = RDX_FILE_TRANSFER_STATE_BUSY;
    } else {
        *out = RDX_FILE_TRANSFER_STATE_IDLE;
    }
    return RDX_OK;
}

rdx_err_t rdx_file_transfer_domain_get_stopped(int *out)
{
    rdx_file_transfer_compat_status_t status;
    rdx_err_t ret;

    if (!out) {
        return RDX_ERR_INVAL;
    }

    ret = rdx_file_transfer_compat_get_status(&status);
    if (ret != RDX_OK) {
        return ret;
    }
    *out = status.stopped;
    return RDX_OK;
}

rdx_err_t rdx_file_transfer_domain_cleanup_record_disconnect(void)
{
    return rdx_file_transfer_compat_cleanup_record_disconnect();
}

rdx_err_t rdx_file_transfer_domain_cleanup_ble_delayed(void)
{
    return rdx_file_transfer_compat_cleanup_ble_delayed();
}

void rdx_file_transfer_domain_on_tx_done(
    rdx_file_transfer_timer_control_t timer_control,
    const void *timer_ctx)
{
    rdx_file_transfer_compat_on_tx_done(timer_control, timer_ctx);
}

void rdx_file_transfer_domain_retry_on_stuck(void)
{
    rdx_file_transfer_compat_retry_on_stuck();
}
