#ifndef __RDX_FILE_TRANSFER_COMPAT_H__
#define __RDX_FILE_TRANSFER_COMPAT_H__

#include "rdx_err.h"

typedef struct {
    int available;
    int busy;
    int stopped;
} rdx_file_transfer_compat_status_t;

rdx_err_t rdx_file_transfer_compat_get_status(
    rdx_file_transfer_compat_status_t *out);
rdx_err_t rdx_file_transfer_compat_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_compat_cleanup_ble_delayed(void);

#endif
