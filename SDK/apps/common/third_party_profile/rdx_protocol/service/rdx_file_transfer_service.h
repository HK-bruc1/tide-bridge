#ifndef __RDX_FILE_TRANSFER_SERVICE_H__
#define __RDX_FILE_TRANSFER_SERVICE_H__

#include "rdx_err.h"

typedef enum {
    RDX_FILE_TRANSFER_STATE_UNAVAILABLE = 0,
    RDX_FILE_TRANSFER_STATE_IDLE,
    RDX_FILE_TRANSFER_STATE_BUSY,
} rdx_file_transfer_state_t;

typedef enum {
    RDX_FILE_TRANSFER_TIMER_STOP = 0,
    RDX_FILE_TRANSFER_TIMER_START,
} rdx_file_transfer_timer_action_t;

typedef void (*rdx_file_transfer_timer_control_t)(
    rdx_file_transfer_timer_action_t action,
    const void *ctx);

rdx_err_t rdx_file_transfer_get_state(rdx_file_transfer_state_t *out);
rdx_err_t rdx_file_transfer_get_stopped(int *out);
void rdx_file_transfer_on_ble_connected(void);
rdx_err_t rdx_file_transfer_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_cleanup_ble_delayed(void);
void rdx_file_transfer_on_tx_done(
    rdx_file_transfer_timer_control_t timer_control,
    const void *timer_ctx);
void rdx_file_transfer_retry_on_stuck(void);

#endif
