#include "rdx_ops.h"
#include "rdx_jl_lifecycle.h"
#include "rdx_jl_rtc.h"
#include "rdx_rtc.h"
#include "rdx_app_config.h"

/* --- jl7018_shadow time ops instance -------------------------------------- */
static const rdx_time_ops_t g_rdx_time_ops_shadow = {
    .set_time       = rdx_rtc_set_time,
    .get_time       = rdx_rtc_get_time,
    .is_leap_year   = rdx_rtc_is_leap_year,
    .days_in_month  = rdx_rtc_days_in_month,
    .is_hw_rtc      = (RDX_RTC_PATH_SEL == RDX_RTC_PATH_HARDWARE),
};

const rdx_time_ops_t *rdx_time_ops_get(void)
{
    return &g_rdx_time_ops_shadow;
}

/* --- jl7018_shadow lifecycle ops instance --------------------------------- */
static const rdx_lifecycle_ops_t g_rdx_lifecycle_ops_shadow = {
    .early_init     = rdx_jl_early_init,
    .pre_sleep      = rdx_jl_pre_sleep,
    .post_wakeup    = rdx_jl_post_wakeup,
    .pre_poweroff   = rdx_jl_pre_poweroff,
};

const rdx_lifecycle_ops_t *rdx_lifecycle_ops_get(void)
{
    return &g_rdx_lifecycle_ops_shadow;
}

/* --- jl7018_shadow wifi transport stubs ----------------------------------- */

static int wifi_shadow_open(void *cfg)
{
    (void)cfg;
    return 0;
}

static int wifi_shadow_close(void)
{
    return 0;
}

static int wifi_shadow_control(u32 cmd, void *arg)
{
    (void)cmd; (void)arg;
    return 0;
}

static int wifi_shadow_tx(const u8 *data, u32 len)
{
    (void)data; (void)len;
    return -1;
}

static int wifi_shadow_rx_done(void)
{
    return -1;
}

static const rdx_wifi_transport_ops_t g_wifi_transport_ops_shadow = {
    .open    = wifi_shadow_open,
    .close   = wifi_shadow_close,
    .control = wifi_shadow_control,
    .tx      = wifi_shadow_tx,
    .rx_done = wifi_shadow_rx_done,
};

const rdx_wifi_transport_ops_t *rdx_wifi_transport_ops_get(void)
{
    return &g_wifi_transport_ops_shadow;
}

/* --- ops validation — shared with jl7018 ---------------------------------- */
rdx_err_t rdx_time_ops_validate(const rdx_time_ops_t *ops)
{
    if (!ops)                     return RDX_ERR_INVAL;
    if (!ops->set_time)           return RDX_ERR_INVAL;
    if (!ops->get_time)           return RDX_ERR_INVAL;
    if (!ops->is_leap_year)       return RDX_ERR_INVAL;
    if (!ops->days_in_month)      return RDX_ERR_INVAL;
    return RDX_OK;
}

rdx_err_t rdx_lifecycle_ops_validate(const rdx_lifecycle_ops_t *ops)
{
    if (!ops)                     return RDX_ERR_INVAL;
    if (!ops->early_init)         return RDX_ERR_INVAL;
    if (!ops->pre_sleep)          return RDX_ERR_INVAL;
    if (!ops->post_wakeup)        return RDX_ERR_INVAL;
    if (!ops->pre_poweroff)       return RDX_ERR_INVAL;
    return RDX_OK;
}

rdx_err_t rdx_wifi_transport_ops_validate(const rdx_wifi_transport_ops_t *ops)
{
    if (!ops)         return RDX_ERR_INVAL;
    if (!ops->open)   return RDX_ERR_INVAL;
    if (!ops->close)  return RDX_ERR_INVAL;
    return RDX_OK;
}
