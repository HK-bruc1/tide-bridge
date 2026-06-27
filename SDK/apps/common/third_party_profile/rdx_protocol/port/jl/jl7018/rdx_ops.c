#include "rdx_ops.h"
#include "rdx_jl_lifecycle.h"
#include "rdx_jl_rtc.h"
#include "rdx_rtc.h"
#include "rdx_app_config.h"

/* --- jl7018 time ops instance --------------------------------------------- */
static const rdx_time_ops_t g_rdx_time_ops_jl7018 = {
    .set_time       = rdx_rtc_set_time,
    .get_time       = rdx_rtc_get_time,
    .is_leap_year   = rdx_rtc_is_leap_year,
    .days_in_month  = rdx_rtc_days_in_month,
    .is_hw_rtc      = (RDX_RTC_PATH_SEL == RDX_RTC_PATH_HARDWARE),
};

const rdx_time_ops_t *rdx_time_ops_get(void)
{
    return &g_rdx_time_ops_jl7018;
}

/* --- jl7018 lifecycle ops instance ---------------------------------------- */
static const rdx_lifecycle_ops_t g_rdx_lifecycle_ops_jl7018 = {
    .early_init     = rdx_jl_early_init,
    .pre_sleep      = rdx_jl_pre_sleep,
    .post_wakeup    = rdx_jl_post_wakeup,
    .pre_poweroff   = rdx_jl_pre_poweroff,
};

const rdx_lifecycle_ops_t *rdx_lifecycle_ops_get(void)
{
    return &g_rdx_lifecycle_ops_jl7018;
}

/* --- jl7018 wifi transport wrappers --------------------------------------- */
extern void xxp_esp32_wifi_open(void);
extern void xxp_esp32_wifi_close(void);
extern void xxp_esp32_wifi_poweron_timer_cancel(void);
extern void xxp_esp32_data_transfer_timer_stop(void);
extern void xxp_esp32_data_transfer_timer_start(void);

static int wifi_jl7018_open(void *cfg)
{
    (void)cfg;
    xxp_esp32_wifi_open();
    return 0;
}

static int wifi_jl7018_close(void)
{
    xxp_esp32_wifi_close();
    return 0;
}

static int wifi_jl7018_control(u32 cmd, void *arg)
{
    (void)arg;
    switch (cmd) {
    case RDX_WIFI_CTRL_POWERON_TIMER_CANCEL:
        xxp_esp32_wifi_poweron_timer_cancel();
        break;
    case RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_STOP:
        xxp_esp32_data_transfer_timer_stop();
        break;
    case RDX_WIFI_CTRL_DATA_TRANSFER_TIMER_START:
        xxp_esp32_data_transfer_timer_start();
        break;
    default:
        return -1;
    }
    return 0;
}

static int wifi_jl7018_tx(const u8 *data, u32 len)
{
    (void)data; (void)len;
    return -1;
}

static int wifi_jl7018_rx_done(void)
{
    return -1;
}

static const rdx_wifi_transport_ops_t g_wifi_transport_ops_jl7018 = {
    .open    = wifi_jl7018_open,
    .close   = wifi_jl7018_close,
    .control = wifi_jl7018_control,
    .tx      = wifi_jl7018_tx,
    .rx_done = wifi_jl7018_rx_done,
};

const rdx_wifi_transport_ops_t *rdx_wifi_transport_ops_get(void)
{
    return &g_wifi_transport_ops_jl7018;
}

/* --- ops validation ------------------------------------------------------- */
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
