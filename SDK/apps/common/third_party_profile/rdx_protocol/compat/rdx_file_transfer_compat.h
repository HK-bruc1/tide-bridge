#ifndef __RDX_FILE_TRANSFER_COMPAT_H__
#define __RDX_FILE_TRANSFER_COMPAT_H__

#include "rdx_file_transfer_service.h"

typedef struct {
    int available;
    int busy;
    int stopped;
} rdx_file_transfer_compat_status_t;

rdx_err_t rdx_file_transfer_compat_get_status(
    rdx_file_transfer_compat_status_t *out);
rdx_err_t rdx_file_transfer_compat_get_active(int *out);
rdx_err_t rdx_file_transfer_compat_get_sync_busy(int *out);
void rdx_file_transfer_compat_on_ble_connected(void);
rdx_err_t rdx_file_transfer_compat_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_compat_cleanup_ble_delayed(void);
void rdx_file_transfer_compat_on_tx_done(
    rdx_file_transfer_timer_control_t timer_control,
    const void *timer_ctx);
void rdx_file_transfer_compat_retry_on_stuck(void);

#endif
