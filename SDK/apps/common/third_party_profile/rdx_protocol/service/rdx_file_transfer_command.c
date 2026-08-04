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
