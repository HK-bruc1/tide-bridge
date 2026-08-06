#ifndef __RDX_FILE_TRANSFER_DOMAIN_H__
#define __RDX_FILE_TRANSFER_DOMAIN_H__

#include "rdx_file_transfer_service.h"

rdx_err_t rdx_file_transfer_domain_get_state(
    rdx_file_transfer_state_t *out);
rdx_err_t rdx_file_transfer_domain_get_stopped(int *out);
rdx_err_t rdx_file_transfer_domain_get_active(int *out);
rdx_err_t rdx_file_transfer_domain_get_sync_busy(int *out);
void rdx_file_transfer_domain_on_ble_connected(void);
rdx_err_t rdx_file_transfer_domain_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_domain_cleanup_ble_delayed(void);
void rdx_file_transfer_domain_on_tx_done(
    rdx_file_transfer_timer_control_t timer_control,
    const void *timer_ctx);
void rdx_file_transfer_domain_retry_on_stuck(void);

#endif
