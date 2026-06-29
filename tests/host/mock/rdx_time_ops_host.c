#include "rdx_ops.h"
#include "rdx_err.h"

/* Host-only time ops for test_time_ops.c */

static rdx_err_t host_set_time(u32 timestamp)
{
    (void)timestamp;
    return RDX_OK;
}

static u32 host_get_time(void)
{
    return 0;
}

static int host_is_leap_year(int year)
{
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        return 1;
    }
    return 0;
}

static int host_days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && host_is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static const rdx_time_ops_t g_host_time_ops = {
    .set_time      = host_set_time,
    .get_time      = host_get_time,
    .is_leap_year  = host_is_leap_year,
    .days_in_month = host_days_in_month,
    .is_hw_rtc     = 1,
};

const rdx_time_ops_t *rdx_time_ops_get(void)
{
    return &g_host_time_ops;
}

rdx_err_t rdx_time_ops_validate(const rdx_time_ops_t *ops)
{
    if (!ops)                     return RDX_ERR_INVAL;
    if (!ops->set_time)           return RDX_ERR_INVAL;
    if (!ops->get_time)           return RDX_ERR_INVAL;
    if (!ops->is_leap_year)       return RDX_ERR_INVAL;
    if (!ops->days_in_month)      return RDX_ERR_INVAL;
    return RDX_OK;
}
