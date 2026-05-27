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
#include "rdx_rtc.h"
#include "syscfg_id.h"
#include "asm/rtc.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define RTC_RESTORE_INTERVAL            (3 * 60 * 1000) // 5 minutes

#define RTC_DEFAULT_DATE_AND_TIME       "2025-04-01 16:50:06" // Default date and time in "YYYY-MM-DD HH:MM:SS" format

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

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
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

#if 1

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
    time_t rtc_timestamp;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rtc_params.rtc_value = timestamp; // timestamp 单位：秒
    rtc_params.begin_msec = sys_timestamp;
    y_printf("\n===> rtc_value: %d, rtc_params.begin_msec: %d \n", rtc_params.rtc_value, rtc_params.begin_msec);
    syscfg_write(VM_RDX_RTC_INIT_VALUE, &rtc_params, sizeof(time_t));
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
    time_t rtc_timestamp;
    int ms_offset = jiffies_msec2offset(rtc_params.begin_msec, sys_timestamp);
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    syscfg_read(VM_RDX_RTC_INIT_VALUE, &rtc_params, sizeof(time_t));
    rtc_timestamp = rtc_params.rtc_value + ms_offset/1000;
    return rtc_timestamp;
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
    char buffer[64];
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
 * function: rdx_rtc_test
 * description: 测试函数
 * param (void)
 * return (int) 返回值
 **************************************************************************/
int rdx_rtc_test(void) {
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    time_t timestamp = rdx_rtc_get();//1696516496; // 示例时间戳
    char buffer[64];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    puts("-------------------------------------------------- \r");
    rdx_rtc_timestamp_to_utc_string(timestamp, buffer);
    g_printf("UTC Time: %s\r", buffer);

    // const char *utc_string = "2023-10-05 12:34:56"; // 示例UTC时间字符串
    timestamp = rdx_rtc_utc_string_to_timestamp(buffer);
    g_printf("Timestamp: %ld\r", (long)timestamp);

    DateTime dt = rdx_rtc_timestamp_to_datetime(timestamp);

    g_printf("Year: %d\r", dt.year);
    g_printf("Month: %d\r", dt.month);
    g_printf("Day: %d\r", dt.day);
    g_printf("Hour: %d\r", dt.hour);
    g_printf("Minute: %d\r", dt.minute);
    g_printf("Second: %d\r", dt.second);
    g_printf("Weekday: %d\r", dt.weekday);
    g_printf("Weekday_name: %s\r", dt.weekday_name);

    return 0;
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
    char tmp_buffer[100];
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rtc_timestamp = rdx_rtc_get();
    if(rtc_timestamp == 0){
        memset(&rtc_params, 0, sizeof(RTCParams));
        //if no rtc timestamp, set default date and time.
        rtc_timestamp = rdx_rtc_utc_string_to_timestamp(RTC_DEFAULT_DATE_AND_TIME);
    }
    //show current time string.
    rdx_rtc_timestamp_to_utc_string(rtc_timestamp, tmp_buffer);
    g_printf("rtc_timestamp: %ld, tmp_buffer: %s \r", (long)rtc_timestamp, tmp_buffer);

    //set current utc timestamp.
    rdx_rtc_set_timestamp(rtc_timestamp);

    //start timer to store rtc timestamp.
    rdx_rtc_restore_timer_start();
}

#else

void rdx_rtc_timer_to_check_time(void *priv)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    rdx_rtc_get();
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
    
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    RTC_DEV_PLATFORM_DATA_BEGIN(rtc_dev_data)
        .default_sys_time = NULL,
        .default_alarm = NULL,
        .cbfun = NULL,
        .clk_sel = CLK_SEL_LRC,
    RTC_DEV_PLATFORM_DATA_END()

    // 调用 RTC 初始化函数
    rtc_init(&rtc_dev_data);

    time_t timestamp = rdx_rtc_get();
    if(timestamp == 0){
        // 设置默认时间
        struct sys_time default_time;
        default_time.year = 2025;
        default_time.month = 4;
        default_time.day = 1;
        default_time.hour = 0;
        default_time.min = 0;
        default_time.sec = 0;

        write_sys_time(&default_time);  // 写入默认时间
        y_printf("=== %s --> set default time: %04d-%02d-%02d %02d:%02d:%02d ===\n", __func__, default_time.year, default_time.month, default_time.day, default_time.hour, default_time.min, default_time.sec);
    }

    //for test.
    // sys_timer_add(NULL, rdx_rtc_timer_to_check_time, 1000);
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
    DateTime d;
    struct sys_time set_cur_time;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    memset(&d, 0, sizeof(DateTime));

    d = rdx_rtc_timestamp_to_datetime(timestamp);

    set_cur_time.year = d.year;
    set_cur_time.month = d.month;
    set_cur_time.day = d.day;
    set_cur_time.hour = d.hour;
    set_cur_time.min = d.minute;
    set_cur_time.sec = d.second;

    y_printf("=== %s --> %04d-%02d-%02d %02d:%02d:%02d ===\r", __func__, set_cur_time.year, set_cur_time.month, set_cur_time.day, set_cur_time.hour, set_cur_time.min, set_cur_time.sec);

    write_sys_time(&set_cur_time);

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
    DateTime dt;
    time_t time_stamp;
    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    read_sys_time(&cur_time);
    b_printf("=== %s --> read sys time: %d-%d-%d %d:%d:%d ===\r", __func__, cur_time.year, cur_time.month, cur_time.day, cur_time.hour, cur_time.min, cur_time.sec);

    dt.year = cur_time.year;
    dt.month = cur_time.month;
    dt.day = cur_time.day;
    dt.hour = cur_time.hour;
    dt.minute = cur_time.min;
    dt.second = cur_time.sec;
    time_stamp = rdx_rtc_datetime_to_timestamp(dt);

    char buffer[30];
    memset(buffer, 0, sizeof(buffer));
    rdx_rtc_timestamp_to_timezone_string(time_stamp, 8, buffer);
    y_printf("======================================================================\r");
    y_printf("=== %s --> get timestamp = %d \r", __func__, time_stamp);
    y_printf("=== %s --> get time = %s \r", __func__, buffer);
    y_printf("======================================================================\r");
    return time_stamp;
}

#endif
