#include "rdx_jl_rtc.h"
#include "system/includes.h"
#include "rtc/rtc_dev.h"
#include "rdx_rtc.h"

/* jl7018_shadow: this chip family has no P11 cache — always read directly from RTC */

rdx_err_t rdx_rtc_set_time(u32 timestamp)
{
	struct sys_time sys_time;
	time_t t = (time_t)timestamp;
	DateTime dt;

	dt = rdx_rtc_timestamp_to_datetime(t);
	memset(&sys_time, 0, sizeof(sys_time));
	sys_time.year  = dt.year;
	sys_time.month = dt.month;
	sys_time.day   = dt.day;
	sys_time.hour  = dt.hour;
	sys_time.min   = dt.minute;
	sys_time.sec   = dt.second;
	rtc_write_time(&sys_time);
	return RDX_OK;
}

u32 rdx_rtc_get_time(void)
{
	struct sys_time sys_time;
	DateTime dt;
	time_t ts;

	/* no P11 on this chip family — read directly from RTC register */
	rtc_read_time(&sys_time);

	dt.year   = sys_time.year;
	dt.month  = sys_time.month;
	dt.day    = sys_time.day;
	dt.hour   = sys_time.hour;
	dt.minute = sys_time.min;
	dt.second = sys_time.sec;

	ts = rdx_rtc_datetime_to_timestamp(dt);
	return (u32)ts;
}
