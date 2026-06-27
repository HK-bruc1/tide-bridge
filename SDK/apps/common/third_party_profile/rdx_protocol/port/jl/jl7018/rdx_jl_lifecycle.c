#include "rdx_jl_lifecycle.h"
#include "system/includes.h"
#include "app_config.h"

/*
 * jl7018 chip family lifecycle implementation.
 * For the current chip family, these are minimal pass-through hooks.
 */

void rdx_jl_early_init(void)
{
    /* jl7018: no chip-specific early init required */
}

int rdx_jl_pre_sleep(void)
{
    /* jl7018: always allow sleep */
    return 0;
}

void rdx_jl_post_wakeup(void)
{
    /* jl7018: no chip-specific wakeup handling required */
}

void rdx_jl_pre_poweroff(void)
{
    /* jl7018: flush pending writes before soft poweroff */
    extern void rdx_rtc_store_timestamp(void);
    rdx_rtc_store_timestamp();
}
