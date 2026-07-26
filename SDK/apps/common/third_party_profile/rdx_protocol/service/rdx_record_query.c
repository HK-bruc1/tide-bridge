#include "rdx_record_service.h"
#include "../internal/rdx_record_domain.h"

bool rdx_record_service_is_running(void)
{
    rdx_record_activity_t activity;
    return rdx_record_domain_get_activity(&activity) == RDX_OK &&
           activity == RDX_RECORD_ACTIVITY_ACTIVE;
}

bool rdx_record_service_can_auto_shutdown(void)
{
    rdx_record_activity_t activity;
    return rdx_record_domain_get_activity(&activity) == RDX_OK &&
           activity == RDX_RECORD_ACTIVITY_IDLE;
}

u8 rdx_record_service_is_active(void)
{
    return 0;
}

rdx_err_t rdx_record_service_get_activity(rdx_record_activity_t *out)
{
    return rdx_record_domain_get_activity(out);
}

rdx_err_t rdx_record_service_get_scene(rdx_record_scene_t *out)
{
    return rdx_record_domain_get_scene(out);
}

rdx_err_t rdx_record_service_get_path(rdx_record_path_t *out)
{
    return rdx_record_domain_get_path(out);
}

bool rdx_record_service_is_offline_active(void)
{
    return rdx_record_domain_is_offline_active();
}
