#include "rdx_file_transfer_service.h"
#include "../internal/rdx_file_transfer_domain.h"

rdx_err_t rdx_file_transfer_get_state(rdx_file_transfer_state_t *out)
{
    return rdx_file_transfer_domain_get_state(out);
}

rdx_err_t rdx_file_transfer_get_stopped(int *out)
{
    return rdx_file_transfer_domain_get_stopped(out);
}

rdx_err_t rdx_file_transfer_get_active(int *out)
{
    return rdx_file_transfer_domain_get_active(out);
}

rdx_err_t rdx_file_transfer_get_sync_busy(int *out)
{
    return rdx_file_transfer_domain_get_sync_busy(out);
}
