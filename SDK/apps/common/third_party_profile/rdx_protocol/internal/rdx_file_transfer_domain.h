#ifndef __RDX_FILE_TRANSFER_DOMAIN_H__
#define __RDX_FILE_TRANSFER_DOMAIN_H__

#include "rdx_file_transfer_service.h"

rdx_err_t rdx_file_transfer_domain_get_state(
    rdx_file_transfer_state_t *out);
rdx_err_t rdx_file_transfer_domain_get_stopped(int *out);
rdx_err_t rdx_file_transfer_domain_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_domain_cleanup_ble_delayed(void);

#endif
