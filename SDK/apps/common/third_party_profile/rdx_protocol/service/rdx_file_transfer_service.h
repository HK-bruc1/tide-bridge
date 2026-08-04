#ifndef __RDX_FILE_TRANSFER_SERVICE_H__
#define __RDX_FILE_TRANSFER_SERVICE_H__

#include "rdx_err.h"

typedef enum {
    RDX_FILE_TRANSFER_STATE_UNAVAILABLE = 0,
    RDX_FILE_TRANSFER_STATE_IDLE,
    RDX_FILE_TRANSFER_STATE_BUSY,
} rdx_file_transfer_state_t;

rdx_err_t rdx_file_transfer_get_state(rdx_file_transfer_state_t *out);
rdx_err_t rdx_file_transfer_get_stopped(int *out);
rdx_err_t rdx_file_transfer_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_cleanup_ble_delayed(void);

#endif
