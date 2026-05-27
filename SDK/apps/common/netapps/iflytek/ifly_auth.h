#pragma once
#include "generic/includes.h"
#include "sys_time.h"
#include <time.h>
#ifdef TCFG_IFLYTEK_APP_SECRET
#define API_SECRET   			TCFG_IFLYTEK_APP_SECRET
#else
#define API_SECRET   			"123"
#endif

#ifdef TCFG_IFLYTEK_APP_KEY
#define API_KEY      			TCFG_IFLYTEK_APP_KEY
#else
#define API_KEY      			"123"
#endif


// 鉴权信息结构体
typedef struct {
    char sig_ori[100];
    char sig_hmac[100];
    char sig_hmac_base64[50];
    char author_ori[200];
    char author[300];
    char date_str[50];
    struct sys_time curtime;
    struct tm pt;
} auth_info_t;


extern struct tm *gmtime(const time_t *timer);

extern char *ifly_authentication(char *host_name, char *host, char *path);

extern size_t strftime_2(char *ptr, size_t maxsize, const char *format, const struct tm *timeptr);
