#include "rdx_jl_rtc.h"
#include "system/includes.h"
#include "rtc/rtc_dev.h"
#include "rdx_rtc.h"

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
	struct _rtc_trim rtc_trim = {0};
	DateTime dt;
	time_t ts;

	if (read_p11_sys_time(&sys_time, &rtc_trim) == 0) {
		rtc_read_time(&sys_time);
	}

	dt.year   = sys_time.year;
	dt.month  = sys_time.month;
	dt.day    = sys_time.day;
	dt.hour   = sys_time.hour;
	dt.minute = sys_time.min;
	dt.second = sys_time.sec;

	ts = rdx_rtc_datetime_to_timestamp(dt);
	return (u32)ts;
}
