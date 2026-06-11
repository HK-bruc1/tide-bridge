/*=====================================================================================
 HEADER NAME: rdx_rtc.c
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Intergration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-03-08 17:36:01
 LastEditors: sheng.dong
 LastEditTime: 2025-03-08 17:36:06
 FilePath: \SDK\apps\earphone\third_part\tuya\rdx_rtc.c
 
 Self-documenting Code
=====================================================================================*/


/******************************************************************************
* Include files
******************************************************************************/ 
#include "app_main.h"
#include "rdx_app_config.h"
#include "rdx_rtc.h"
#include "syscfg_id.h"
#include "rtc/rtc_dev.h"
#include "system/generic/jiffies.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define RTC_RESTORE_INTERVAL            (3 * 60 * 1000) // 5 minutes

#define RTC_DEFAULT_DATE_AND_TIME       "2000-01-01 00:00:00" // Default date and time in "YYYY-MM-DD HH:MM:SS" format
#define RDX_RTC_TIMEZONE_OFFSET_SEC     (8 * 3600) // UTC+8, hardcoded for now

/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
typedef struct{
    time_t rtc_value;
    unsigned long begin_msec;
}RTCParams;

/******************************************************************************
* Global Variables Section
******************************************************************************/ 

/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static u16 rtc_restore_timer = 0;
static RTCParams rtc_params;
static u8 rdx_rtc_boot_time_valid = 0;

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
extern const struct sys_time def_sys_time;

time_t rdx_rtc_get(void);

/******************************************************************************
* Function Section
******************************************************************************/ 

/**************************************************************************
 * function: rdx_rtc_is_leap_year
 * description: 判断是否是闰年
 * param (int) year
 * return (*)
 **************************************************************************/
int rdx_rtc_is_leap_year(int year) 
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/ 
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**************************************************************************
 * function: rdx_rtc_days_in_month
 * description: 获取某年的月份天数
 * param (int) year
 * param (int) month
 * return (int) 月份天数
 **************************************************************************/
int rdx_rtc_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && rdx_rtc_is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

/**************************************************************************
 * function: rdx_rtc_calculate_weekday
 * description: 计算星期几（基姆拉尔森公式）
 * param (int) year
 * param (int) month
 * param (int) day
 * return (int) 星期几（0=周日，1=周一，...，6=周六）
 **************************************************************************/
int rdx_rtc_calculate_weekday(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year -= 1;
    }
    int K = year % 100; // 年份的后两位
    int J = year / 100; // 年份的前两位

    // Zeller 公式
    int weekday = (day + 13 * (month + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    return weekday;
}

/**************************************************************************
 * function: rdx_rtc_timestamp_to_datetime
 * description: 将时间戳转换为 DateTime 结构体
 * param (time_t) timestamp
 * return (DateTime) DateTime 结构体
 **************************************************************************/
DateTime rdx_rtc_timestamp_to_datetime(time_t timestamp) {
    DateTime dt = {1970, 1, 1, 0, 0, 0, 4}; // 起始时间：1970-01-01 00:00:00（星期四）

    // 计算年
    while (1) {
        int days_in_year = rdx_rtc_is_leap_year(dt.year) ? 366 : 365;
        time_t seconds_in_year = days_in_year * 86400;
        if (timestamp < seconds_in_year) {
            break;
        }
        timestamp -= seconds_in_year;
        dt.year++;
    }

    // 计算月
    while (1) {
        int days = rdx_rtc_days_in_month(dt.year, dt.month);
        time_t seconds_in_month = days * 86400;
        if (timestamp < seconds_in_month) {
            break;
        }
        timestamp -= seconds_in_month;
        dt.month++;
    }

    // 计算日
    dt.day += timestamp / 86400;
    timestamp %= 86400;

    // 计算时、分、秒
    dt.hour = timestamp / 3600;
    timestamp %= 3600;
    dt.minute = timestamp / 60;
    dt.second = timestamp % 60;

    // 计算星期几
    dt.weekday = rdx_rtc_calculate_weekday(dt.year, dt.month, dt.day);

    // 星期几映射
    const char *weekdays[] = {"Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};
    dt.weekday_name = weekdays[dt.weekday]; // 假设 DateTime 结构体中有一个 weekday_name 字段

    return dt;
}

/**************************************************************************
 * function: rdx_rtc_timestamp_to_timezone_string
 * description: 将UTC时间戳转换为指定时区的时间字符串
 * param (time_t) utc_timestamp
 * param (int) timezone_offset
 * param (char) *buffer
 * return (*)
 **************************************************************************/
void rdx_rtc_timestamp_to_timezone_string(time_t utc_timestamp, int timezone_offset, char *buffer) {
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    // int timezone_offset = -18000; // 示例时区偏移量（例如，美国东部时间 -5小时）
    
    // 应用时区偏移量
    time_t local_timestamp = utc_timestamp + timezone_offset;

    // 转换为DateTime结构体
    DateTime dt = rdx_rtc_timestamp_to_datetime(local_timestamp);

    // 格式化输出
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

/**************************************************************************
 * function: rdx_rtc_datetime_to_timestamp
 * description: 将 DateTime 结构体转换为时间戳
 * param (DateTime) dt
 * return (time_t) 时间戳
 **************************************************************************/
time_t rdx_rtc_datetime_to_timestamp(DateTime dt) {
    time_t timestamp = 0;

    // 计算年的秒数
    for (int y = 1970; y < dt.year; y++) {
        timestamp += rdx_rtc_is_leap_year(y) ? 366 * 86400 : 365 * 86400;
    }

    // 计算月的秒数
    for (int m = 1; m < dt.month; m++) {
        timestamp += rdx_rtc_days_in_month(dt.year, m) * 86400;
    }

    // 计算日的秒数
    timestamp += (dt.day - 1) * 86400;

    // 计算时、分、秒
    timestamp += dt.hour * 3600 + dt.minute * 60 + dt.second;

    return timestamp;
}

/**************************************************************************
 * function: rdx_rtc_timestamp_to_utc_string
 * description: 将时间戳转换为UTC时间字符串
 * param (time_t) timestamp
 * param (char*) buffer
 * return (void)
 **************************************************************************/
void rdx_rtc_timestamp_to_utc_string(time_t timestamp, char *buffer) {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;

    // 计算年
    while (1) {
        int days_in_year = rdx_rtc_is_leap_year(year) ? 366 : 365;
        if (timestamp < days_in_year * 86400) {
            break;
        }
        timestamp -= days_in_year * 86400;
        year++;
    }

    // 计算月
    while (1) {
        int days = rdx_rtc_days_in_month(year, month);
        if (timestamp < days * 86400) {
            break;
        }
        timestamp -= days * 86400;
        month++;
    }

    // 计算日
    day += timestamp / 86400;
    timestamp %= 86400;

    // 计算时、分、秒
    hour = timestamp / 3600;
    timestamp %= 3600;
    minute = timestamp / 60;
    second = timestamp % 60;

    // 格式化输出
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
}

/**************************************************************************
 * function: rdx_rtc_parse_utc_string
 * description: 自定义字符串解析函数
 * param (char) *utc_string
 * param (int) *year
 * param (int) *month
 * param (int) *day
 * param (int) *hour
 * param (int) *minute
 * param (int) *second
 * return (*)
 **************************************************************************/
void rdx_rtc_parse_utc_string(const char *utc_string, int *year, int *month, int *day, int *hour, int *minute, int *second) {
    // 假设输入格式为 "YYYY-MM-DD HH:MM:SS"
    *year = (utc_string[0] - '0') * 1000 + (utc_string[1] - '0') * 100 +
            (utc_string[2] - '0') * 10 + (utc_string[3] - '0');
    *month = (utc_string[5] - '0') * 10 + (utc_string[6] - '0');
    *day = (utc_string[8] - '0') * 10 + (utc_string[9] - '0');
    *hour = (utc_string[11] - '0') * 10 + (utc_string[12] - '0');
    *minute = (utc_string[14] - '0') * 10 + (utc_string[15] - '0');
    *second = (utc_string[17] - '0') * 10 + (utc_string[18] - '0');
}

/**************************************************************************
 * function: rdx_rtc_utc_string_to_timestamp
 * description: 计算从1970年1月1日到指定日期的总秒数
 * param (const char*) utc_string
 * return (time_t) 时间戳
 **************************************************************************/
time_t rdx_rtc_utc_string_to_timestamp(const char *utc_string) {
    int year, month, day, hour, minute, second;

    // 使用自定义解析函数
    rdx_rtc_parse_utc_string(utc_string, &year, &month, &day, &hour, &minute, &second);

    time_t timestamp = 0;

    // 计算年的秒数
    for (int y = 1970; y < year; y++) {
        timestamp += rdx_rtc_is_leap_year(y) ? 366 * 86400 : 365 * 86400;
    }

    // 计算月的秒数
    for (int m = 1; m < month; m++) {
        timestamp += rdx_rtc_days_in_month(year, m) * 86400;
    }

    // 计算日的秒数
    timestamp += (day - 1) * 86400;

    // 计算时、分、秒
    timestamp += hour * 3600 + minute * 60 + second;

    return timestamp;
}

static time_t rdx_rtc_read_vm_timestamp(void)
{
    time_t timestamp = 0;

    syscfg_read(VM_RDX_RTC_INIT_VALUE, &timestamp, sizeof(timestamp));
    return timestamp;
}

static void rdx_rtc_write_vm_timestamp(time_t timestamp)
{
    syscfg_write(VM_RDX_RTC_INIT_VALUE, &timestamp, sizeof(timestamp));
}

static void rdx_rtc_datetime_to_sys_time(const DateTime *datetime, struct sys_time *sys_time)
{
    memset(sys_time, 0, sizeof(*sys_time));
    sys_time->year = datetime->year;
    sys_time->month = datetime->month;
    sys_time->day = datetime->day;
    sys_time->hour = datetime->hour;
    sys_time->min = datetime->minute;
    sys_time->sec = datetime->second;
}

static void rdx_rtc_sys_time_to_datetime(const struct sys_time *sys_time, DateTime *datetime)
{
    memset(datetime, 0, sizeof(*datetime));
    datetime->year = sys_time->year;
    datetime->month = sys_time->month;
    datetime->day = sys_time->day;
    datetime->hour = sys_time->hour;
    datetime->minute = sys_time->min;
    datetime->second = sys_time->sec;
}

static int rdx_rtc_sys_time_valid(const struct sys_time *sys_time)
{
    if (sys_time->year < 1970 || sys_time->month < 1 || sys_time->month > 12) {
        return 0;
    }

    if (sys_time->day < 1 || sys_time->day > 31) {
        return 0;
    }

    if (sys_time->hour > 23 || sys_time->min > 59 || sys_time->sec > 59) {
        return 0;
    }

    return 1;
}

static int rdx_rtc_read_hw_time(struct sys_time *sys_time)
{
    struct _rtc_trim rtc_trim = {0};

    memset(sys_time, 0, sizeof(*sys_time));

    if (read_p11_sys_time(sys_time, &rtc_trim) && rdx_rtc_sys_time_valid(sys_time)) {
        return 1;
    }

    rtc_read_time(sys_time);
    return 0;
}

static void rdx_rtc_write_hw_time(const struct sys_time *sys_time)
{
    rtc_write_time(sys_time);
}

static time_t rdx_rtc_sys_time_to_timestamp(const struct sys_time *sys_time)
{
    DateTime datetime;

    rdx_rtc_sys_time_to_datetime(sys_time, &datetime);
    return rdx_rtc_datetime_to_timestamp(datetime);
}

static void rdx_rtc_timestamp_to_sys_time_value(time_t timestamp, struct sys_time *sys_time)
{
    DateTime datetime = rdx_rtc_timestamp_to_datetime(timestamp);

    rdx_rtc_datetime_to_sys_time(&datetime, sys_time);
}

static int rdx_rtc_is_init_placeholder_time(const struct sys_time *sys_time)
{
    if (!rdx_rtc_sys_time_valid(sys_time)) {
        return 0;
    }

    return (sys_time->year == def_sys_time.year &&
            sys_time->month == def_sys_time.month &&
            sys_time->day == def_sys_time.day);
}

static void rdx_rtc_capture_boot_time(const struct sys_time *sys_time)
{
    if (!rdx_rtc_boot_time_valid &&
        rdx_rtc_sys_time_valid(sys_time) &&
        !rdx_rtc_is_init_placeholder_time(sys_time)) {
        rdx_rtc_boot_time_valid = 1;
    }
}

/**************************************************************************
 * function: rdx_rtc_test
 * description: 测试函数
 * param (void)
 * return (int) 返回值
 **************************************************************************/
int rdx_rtc_test(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    time_t timestamp = rdx_rtc_get();
    time_t parsed_timestamp = 0;
    char buffer[64];
    DateTime dt;
#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_HARDWARE)
    const char *rtc_path = "HW";
#else
    const char *rtc_path = "SW";
#endif
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_rtc_timestamp_to_utc_string(timestamp, buffer);
    parsed_timestamp = rdx_rtc_utc_string_to_timestamp(buffer);
    dt = rdx_rtc_timestamp_to_datetime(timestamp);

    g_printf("[RDX_RTC][%s] ts=%ld utc=%s wd=%d rt=%s\r",
             rtc_path,
             (long)timestamp,
             buffer,
             dt.weekday,
             (parsed_timestamp == timestamp) ? "ok" : "bad");

    if (parsed_timestamp != timestamp) {
        g_printf("[RDX_RTC][%s] WARN parsed=%ld src=%ld\r",
                 rtc_path,
                 (long)parsed_timestamp,
                 (long)timestamp);
        return -1;
    }

    return 0;
}

#if (RDX_RTC_PATH_SEL == RDX_RTC_PATH_SOFTWARE)

/**************************************************************************
 * function: rdx_rtc_set_timestamp
 * description: 
 * param (time_t) timestamp
 * return (int) 
 **************************************************************************/
int rdx_rtc_set_timestamp(time_t timestamp)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    unsigned long sys_timestamp = jiffies_msec();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rtc_params.rtc_value = timestamp; // timestamp 单位：秒
    rtc_params.begin_msec = sys_timestamp;
    y_printf("\n===> rtc_value: %ld, rtc_params.begin_msec: %lu \n", (long)rtc_params.rtc_value, rtc_params.begin_msec);
    rdx_rtc_write_vm_timestamp(timestamp);
    return 0;
}

/**************************************************************************
 * function: rdx_app_rtc_get
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
time_t rdx_rtc_get(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    unsigned long sys_timestamp = jiffies_msec();
    int ms_offset = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if (rtc_params.rtc_value == 0) {
        rtc_params.rtc_value = rdx_rtc_read_vm_timestamp();
        rtc_params.begin_msec = sys_timestamp;
    }

    if (rtc_params.rtc_value == 0) {
        return 0;
    }

    ms_offset = jiffies_msec2offset(rtc_params.begin_msec, sys_timestamp);
    return rtc_params.rtc_value + ms_offset / 1000;
}

/**************************************************************************
 * function: rdx_rtc_get_string
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
char* rdx_rtc_get_string(void) 
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    static char buffer[64];
    time_t timestamp = rdx_rtc_get();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_rtc_timestamp_to_utc_string(timestamp, buffer);
    return buffer;
}

/**************************************************************************
 * function: rdx_rtc_get_string_date
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
char* rdx_rtc_get_string_date(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    static char date_buffer[32];
    time_t timestamp = rdx_rtc_get();
    DateTime dt = rdx_rtc_timestamp_to_datetime(timestamp);
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(date_buffer, 0, sizeof(date_buffer));
    sprintf(date_buffer, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    return date_buffer;
}

/**************************************************************************
 * function: rdx_rtc_get_string_time
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
char* rdx_rtc_get_string_time(void) 
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    static char time_buffer[32];
    time_t timestamp = rdx_rtc_get();
    DateTime dt = rdx_rtc_timestamp_to_datetime(timestamp);
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(time_buffer, 0, sizeof(time_buffer));
    sprintf(time_buffer, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    return time_buffer;
}

/**************************************************************************
 * function: rdx_rtc_store_timestamp
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_store_timestamp(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    time_t timestamp = rdx_rtc_get();
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_rtc_set_timestamp(timestamp);
}

/**************************************************************************
 * function: rdx_rtc_restore_timer_stop
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_restore_timer_stop(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rtc_restore_timer != 0){
        sys_timer_del(rtc_restore_timer);
        rtc_restore_timer = 0;
    }
}

/**************************************************************************
 * function: rdx_rtc_restore_timer_start
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_restore_timer_start(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    if(rtc_restore_timer == 0){
        rtc_restore_timer = sys_timer_add(NULL, rdx_rtc_store_timestamp, RTC_RESTORE_INTERVAL);
    }
}

/**************************************************************************
 * function: rdx_rtc_init
 * description: RTC初始化函数
 * param (void)
 * return (void)
 **************************************************************************/
void rdx_rtc_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    time_t rtc_timestamp;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(&rtc_params, 0, sizeof(rtc_params));
    rtc_timestamp = rdx_rtc_get();
    if(rtc_timestamp == 0){
        //if no rtc timestamp, set default date and time.
        rtc_timestamp = rdx_rtc_utc_string_to_timestamp(RTC_DEFAULT_DATE_AND_TIME);
    }

    //set current utc timestamp.
    rdx_rtc_set_timestamp(rtc_timestamp);

    //start timer to store rtc timestamp.
    rdx_rtc_restore_timer_start();
}

#else

/**************************************************************************
 * function: rdx_rtc_store_timestamp
 * description:
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_store_timestamp(void)
{
    rdx_rtc_write_vm_timestamp(rdx_rtc_get());
}

/**************************************************************************
 * function: rdx_rtc_restore_timer_stop
 * description:
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_restore_timer_stop(void)
{
}

/**************************************************************************
 * function: rdx_rtc_restore_timer_start
 * description:
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_restore_timer_start(void)
{
}

/**************************************************************************
 * function: rdx_rtc_init
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_rtc_init(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    struct sys_time current_time = {0};
    time_t rtc_timestamp = 0;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    char utc_buf[32];

    rdx_rtc_read_hw_time(&current_time);
    if (!rdx_rtc_sys_time_valid(&current_time)) {
        rtc_timestamp = rdx_rtc_read_vm_timestamp();
        if (rtc_timestamp == 0) {
            rtc_timestamp = rdx_rtc_sys_time_to_timestamp(&def_sys_time);
            g_printf("[RDX_RTC][HW] init: hw=invalid vm=empty use=DEFAULT\r");
        } else {
            rdx_rtc_timestamp_to_timezone_string(rtc_timestamp, RDX_RTC_TIMEZONE_OFFSET_SEC, utc_buf);
            g_printf("[RDX_RTC][HW] init: hw=invalid utc=%s src=VM\r", utc_buf);
        }

        rdx_rtc_set_timestamp(rtc_timestamp);
        rdx_rtc_read_hw_time(&current_time);
    } else if (rdx_rtc_is_init_placeholder_time(&current_time)) {
        rtc_timestamp = rdx_rtc_read_vm_timestamp();
        if (rtc_timestamp != 0) {
            rdx_rtc_timestamp_to_timezone_string(rtc_timestamp, RDX_RTC_TIMEZONE_OFFSET_SEC, utc_buf);
            g_printf("[RDX_RTC][HW] init: hw=default utc=%s src=VM\r", utc_buf);
            rdx_rtc_set_timestamp(rtc_timestamp);
            rdx_rtc_read_hw_time(&current_time);
        } else {
            g_printf("[RDX_RTC][HW] init: hw=default vm=empty use=HW\r");
        }
    } else {
        rdx_rtc_timestamp_to_timezone_string(rdx_rtc_sys_time_to_timestamp(&current_time), RDX_RTC_TIMEZONE_OFFSET_SEC, utc_buf);
        g_printf("[RDX_RTC][HW] init: hw=running utc=%s src=RTC sync=VM\r", utc_buf);
        rdx_rtc_write_vm_timestamp(rdx_rtc_sys_time_to_timestamp(&current_time));
    }

    rdx_rtc_capture_boot_time(&current_time);
}

/**************************************************************************
 * function: rdx_rtc_set_timestamp
 * description: 
 * param (time_t) timestamp
 * return (int) 
 **************************************************************************/
int rdx_rtc_set_timestamp(time_t timestamp)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    struct sys_time set_cur_time;
    char utc_buf[32];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_rtc_timestamp_to_timezone_string(timestamp, RDX_RTC_TIMEZONE_OFFSET_SEC, utc_buf);
    g_printf("[RDX_RTC][HW] set: utc=%s\r", utc_buf);

    rdx_rtc_timestamp_to_sys_time_value(timestamp, &set_cur_time);

    rdx_rtc_write_hw_time(&set_cur_time);
    rdx_rtc_write_vm_timestamp(timestamp);

    return 0;
}

/**************************************************************************
 * function: rdx_rtc_get
 * description: 
 * param (*)
 * return (*)
 **************************************************************************/
time_t rdx_rtc_get(void)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    struct sys_time cur_time;
    time_t time_stamp;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    char utc_buf[32];

    memset(&cur_time, 0, sizeof(cur_time));

    rdx_rtc_read_hw_time(&cur_time);
    if (!rdx_rtc_sys_time_valid(&cur_time)) {
        time_stamp = rdx_rtc_read_vm_timestamp();
        rdx_rtc_timestamp_to_timezone_string(time_stamp, RDX_RTC_TIMEZONE_OFFSET_SEC, utc_buf);
        g_printf("[RDX_RTC][HW] utc=%s src=VM\r", utc_buf);
        return time_stamp;
    }

    time_stamp = rdx_rtc_sys_time_to_timestamp(&cur_time);

    if (!rdx_rtc_boot_time_valid) {
        rdx_rtc_capture_boot_time(&cur_time);
        if (rdx_rtc_boot_time_valid) {
            rdx_rtc_write_vm_timestamp(time_stamp);
        }
    }

    rdx_rtc_timestamp_to_timezone_string(time_stamp, RDX_RTC_TIMEZONE_OFFSET_SEC, utc_buf);
    g_printf("[RDX_RTC][HW] utc=%s src=RTC\r", utc_buf);
    return time_stamp;
}

#endif

/**************************************************************************
 * function: rdx_cpu_reset
 * description: 保存RTC到VM后执行cpu_reset
 * param (*)
 * return (*)
 **************************************************************************/
void rdx_cpu_reset(void)
{
    rdx_rtc_store_timestamp();
    cpu_reset();
}
