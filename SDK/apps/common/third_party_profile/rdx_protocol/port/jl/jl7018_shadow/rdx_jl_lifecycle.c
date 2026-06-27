#include "rdx_jl_lifecycle.h"
#include "system/includes.h"
#include "app_config.h"

/*
 * jl7018_shadow lifecycle implementation.
 * Simulates a chip family with different sleep/wakeup behavior.
 */

void rdx_jl_early_init(void)
{
    /* jl7018_shadow: init extra GPIO bank for this chip family */
}

int rdx_jl_pre_sleep(void)
{
    /* jl7018_shadow: check if SPI/WiFi is idle before allowing sleep */
    return 0;
}

void rdx_jl_post_wakeup(void)
{
    /* jl7018_shadow: re-init clock domain after wakeup */
}

void rdx_jl_pre_poweroff(void)
{
    /* jl7018_shadow: extended cleanup before soft poweroff */
    extern void rdx_rtc_store_timestamp(void);
    rdx_rtc_store_timestamp();
}
