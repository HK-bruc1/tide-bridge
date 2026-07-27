#include "rdx_record_service.h"
#include "../internal/rdx_record_domain.h"

rdx_err_t rdx_record_service_stop_now(rdx_record_stop_reason_t reason)
{
    return rdx_record_domain_stop_now(reason);
}

rdx_err_t rdx_record_service_stop_post(rdx_record_stop_reason_t reason)
{
    return rdx_record_domain_stop_post(reason);
}
