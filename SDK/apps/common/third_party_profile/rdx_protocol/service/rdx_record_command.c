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

rdx_err_t rdx_record_service_set_path(rdx_record_path_t path)
{
    return rdx_record_domain_set_path(path);
}

rdx_err_t rdx_record_service_mark_key_triggered(void)
{
    return rdx_record_domain_mark_key_triggered();
}

rdx_err_t rdx_record_service_complete_switch(bool *restart,
                                             rdx_record_scene_t *scene)
{
    return rdx_record_domain_complete_switch(restart, scene);
}
