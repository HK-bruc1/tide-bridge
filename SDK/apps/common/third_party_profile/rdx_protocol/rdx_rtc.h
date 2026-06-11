/*=====================================================================================
 HEADER NAME: rdx_rtc.h
 MODULE NAME: application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	This File will gather functions that special handle msg(UserEvent,Timer) from 
 	Integration.These functions don't be changed by project changed.
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: sheng.dong
 Date: 2025-03-08 17:44:39
 LastEditors: sheng.dong
 LastEditTime: 2025-03-08 20:22:41
 FilePath: \SDK\apps\earphone\third_part\tuya\rdx_rtc.h
 
 Self-documenting Code
=====================================================================================*/
#ifndef __RDX_RTC__
#define __RDX_RTC__

/******************************************************************************
* Include files
******************************************************************************/ 


/******************************************************************************
* Macro Define Section
******************************************************************************/ 


/******************************************************************************
* Structure and Enum Section
******************************************************************************/ 
// date & time struct.
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday; // Day of the week (0=Sunday, 1=Monday, ..., 6=Saturday)
    const char *weekday_name;
} DateTime;

/******************************************************************************
* Global Variables Section
******************************************************************************/ 

/******************************************************************************
* Function Section
******************************************************************************/ 

// Determine if a year is a leap year
int rdx_rtc_is_leap_year(int year);

// Get the number of days in a month of a given year
int rdx_rtc_days_in_month(int year, int month);

// Calculate the day of the week using Zeller's Congruence
int rdx_rtc_calculate_weekday(int year, int month, int day);

// Convert a timestamp to a DateTime structure
DateTime rdx_rtc_timestamp_to_datetime(time_t timestamp);

// Convert a DateTime structure to a timestamp
time_t rdx_rtc_datetime_to_timestamp(DateTime dt);

// Convert a timestamp to a UTC time string
void rdx_rtc_timestamp_to_utc_string(time_t timestamp, char *buffer);

// Custom string parsing function
void rdx_rtc_parse_utc_string(const char *utc_string, int *year, int *month, int *day, int *hour, int *minute, int *second);

// Calculate the total seconds from 1970-01-01 to a specified date
time_t rdx_rtc_utc_string_to_timestamp(const char *utc_string);

// Test function
int rdx_rtc_test(void);

// Set the current timestamp
int rdx_rtc_set_timestamp(time_t timestamp);

// Persist the current RDX RTC time if the selected path needs it
void rdx_rtc_store_timestamp(void);

// Start/stop the periodic RTC persistence timer in software mode
void rdx_rtc_restore_timer_stop(void);
void rdx_rtc_restore_timer_start(void);

// RTC initialization function
void rdx_rtc_init(void);

// Save RTC then cpu_reset
void rdx_cpu_reset(void);

#endif