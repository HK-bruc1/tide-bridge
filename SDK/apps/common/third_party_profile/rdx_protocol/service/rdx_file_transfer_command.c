#include "rdx_file_transfer_service.h"
#include "../internal/rdx_file_transfer_domain.h"

rdx_err_t rdx_file_transfer_cleanup_record_disconnect(void)
{
    return rdx_file_transfer_domain_cleanup_record_disconnect();
}

rdx_err_t rdx_file_transfer_cleanup_ble_delayed(void)
{
    return rdx_file_transfer_domain_cleanup_ble_delayed();
}

void rdx_file_transfer_on_tx_done(
    rdx_file_transfer_timer_control_t timer_control,
    const void *timer_ctx)
{
    rdx_file_transfer_domain_on_tx_done(timer_control, timer_ctx);
}

void rdx_file_transfer_retry_on_stuck(void)
{
    rdx_file_transfer_domain_retry_on_stuck();
}
