#include "system/includes.h"
#include "websocket_api.h"
#include "ifly_test_data.h"
#include "cJSON.h"
#include "ifly_common.h"


#if TCFG_IFLYTEK_ENABLE

void *_calloc_r(struct _reent *r, size_t a, size_t b)
{
    return calloc(a, b);
}

void *calloc(unsigned long count, unsigned long size)
{
    void *p;

    p = malloc(count * size);
    if (p) {
        memset(p, 0, count * size);
    }
    return p;
}




static u32 mark_utc = 0;
static u32 cur_sys = 0;
static u32 time_offset = 0;
extern unsigned long jiffies_msec();

time_t time(time_t *timer)
{
    u32 t = jiffies_msec() / 1000 + time_offset;
    printf("~~~~~~~~~~~~~~~~~ %s %d %u\n", __FUNCTION__, __LINE__, t);
    if (timer) {
        * timer = t;
    }
    return t;
}


time_t aaa;
void test_web_demo()
{
    extern void bt_get_time_date();
    bt_get_time_date();
    time(&aaa);
    extern void ifly_net_demo(void);
    ifly_net_demo();
}



#define CPU_RAND()	(JL_RAND->R64L)
extern unsigned int random32(int type);

void get_random_bytes(unsigned char *buf, int nbytes)
{
    while (nbytes--) {
        *buf = random32(0);
        ++buf;
    }
}

int rand_val(void *p_rng, unsigned char *output, unsigned int output_len)
{
    get_random_bytes(output, output_len);
    return 0;
}


u32 timestamp_mytime_2_utc_sec(struct sys_time *mytime);
void get_time_from_bt(u8 *data)
{
    struct sys_time my_time;
    my_time.year = 2000 + (data[0] - '0') * 10 + (data[1] - '0');
    my_time.month = (data[3] - '0') * 10 + (data[4] - '0');
    my_time.day = (data[6] - '0') * 10 + (data[7] - '0');
    my_time.hour = (data[10] - '0') * 10 + (data[11] - '0');
    my_time.min = (data[13] - '0') * 10 + (data[14] - '0');
    my_time.sec = (data[16] - '0') * 10 + (data[17] - '0');
    printf("sys_time %d:%d:%d %d:%d:%d", my_time.year, my_time.month, my_time.day, my_time.hour, my_time.min, my_time.sec);
#define CST_OFFSET_TIME			(8*60*60)	// 北京时间时差
    mark_utc = timestamp_mytime_2_utc_sec(&my_time) - CST_OFFSET_TIME;
    cur_sys = jiffies_msec() / 1000;
    printf(">>>>> %s  %d cur_sys %d \n", __func__, __LINE__, cur_sys);
    time_offset = mark_utc - cur_sys;
    printf(">>>>> %s  %d time %d cur_sys %d time_offset %d\n", __func__, __LINE__, mark_utc, cur_sys, time_offset);
}

struct sys_time m_time;
void my_test_get_time(u8 *data)
{
    m_time.year = 2000 + (data[0] - '0') * 10 + (data[1] - '0');
    m_time.month = (data[3] - '0') * 10 + (data[4] - '0');
    m_time.day = (data[6] - '0') * 10 + (data[7] - '0');
    m_time.hour = (data[10] - '0') * 10 + (data[11] - '0');
    m_time.min = (data[13] - '0') * 10 + (data[14] - '0');
    m_time.sec = (data[16] - '0') * 10 + (data[17] - '0');
    printf("sys_time %d:%d:%d %d:%d:%d", m_time.year, m_time.month, m_time.day, m_time.hour, m_time.min, m_time.sec);
}




#define UTC_BASE_YEAR 		1970
#define MONTH_PER_YEAR 		12
#define DAY_PER_YEAR 		365
#define SEC_PER_DAY 		86400
#define SEC_PER_HOUR 		3600
#define SEC_PER_MIN 		60

/* Number of days per month */
static const u8  days_per_month[MONTH_PER_YEAR] = {
    /*
    1   2   3   4   5   6   7   8   9   10  11  12
    */
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static bool get_is_leap_year(u16 year)
{
    if (((year % 4) == 0) && ((year % 100) != 0)) {
        return 1;
    } else if ((year % 400) == 0) {
        return 1;
    }

    return 0;
}

static u8 get_day_of_mon(u8 month, u16 year)
{
    if ((month == 0) || (month > 12)) {
        return days_per_month[1] + get_is_leap_year(year);
    }

    if (month != 2) {
        return days_per_month[month - 1];
    } else {
        return days_per_month[1] + get_is_leap_year(year);
    }
}

void timestamp_utc_sec_2_mytime(u32 utc_sec, struct sys_time *mytime)
{
    int32_t sec, day;
    u16	y;
    u8 m;
    u16	d;

    sec = utc_sec % SEC_PER_DAY;
    mytime->hour = sec / SEC_PER_HOUR;

    sec %= SEC_PER_HOUR;
    mytime->min = sec / SEC_PER_MIN;

    mytime->sec = sec % SEC_PER_MIN;

    day = utc_sec / SEC_PER_DAY;
    for (y = UTC_BASE_YEAR; day > 0; y++) {
        d = (DAY_PER_YEAR + get_is_leap_year(y));
        if (day >= d) {
            day -= d;
        } else {
            break;
        }
    }

    mytime->year = y;

    for (m = 1; m < MONTH_PER_YEAR; m++) {
        d = get_day_of_mon(m, y);
        if (day >= d) {
            day -= d;
        } else {
            break;
        }
    }

    mytime->month = m;
    mytime->day = (u8)(day + 1);
}

u32 timestamp_mytime_2_utc_sec(struct sys_time *mytime)
{
    u16	i;
    u32 no_of_days = 0;
    u32 utc_time;
    u8  dst = 1;

    if (mytime->year < UTC_BASE_YEAR) {
        return 0;
    }

    for (i = UTC_BASE_YEAR; i < mytime->year; i++) {
        no_of_days += (DAY_PER_YEAR + get_is_leap_year(i));
    }

    for (i = 1; i < mytime->month; i++) {
        no_of_days += get_day_of_mon(i, mytime->year);
    }

    no_of_days += (mytime->day - 1);

    utc_time = (unsigned int) no_of_days * SEC_PER_DAY + (unsigned int)(mytime->hour * SEC_PER_HOUR +
               mytime->min * SEC_PER_MIN + mytime->sec);

    return utc_time;
}
#define WDAY_COUNT 7
#define MON_COUNT 12
static const char wday_name[][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""
};

static const char mon_name[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""
};



size_t strftime_2(char *ptr, size_t maxsize, const char *format, const struct tm *timeptr)
{
    if ((timeptr == NULL) || (ptr == NULL)) {
        return -1;
    }

    if (timeptr->tm_mday < 10) {
        return snprintf(ptr, maxsize, "%s, 0%d %s %d %02d:%02d:%02d GMT",
                        wday_name[timeptr->tm_wday],
                        timeptr->tm_mday,
                        mon_name[timeptr->tm_mon],
                        timeptr->tm_year + 1900,
                        timeptr->tm_hour,
                        timeptr->tm_min,
                        timeptr->tm_sec);
    } else {
        return snprintf(ptr, maxsize, "%s, %d %s %d %02d:%02d:%02d GMT",
                        wday_name[timeptr->tm_wday],
                        timeptr->tm_mday,
                        mon_name[timeptr->tm_mon],
                        timeptr->tm_year + 1900,
                        timeptr->tm_hour,
                        timeptr->tm_min,
                        timeptr->tm_sec);
    }
}






#include "mbedtls/base64.h"
#include "cJSON.h"

#define CHUNK_SIZE 168
#define FILE_PATH "storage/sd0/C/SF.BIN"
#define SPEEX_SIZE 42
#define BASE64_BUF_LEN (CHUNK_SIZE * 4 / 3 + 4)  // Base64编码后最大长度

typedef enum {
    STATUS_FIRST_FRAME = 0,
    STATUS_CONTINUE_FRAME,
    STATUS_LAST_FRAME
} FrameStatus;

// WebSocket发送接口声明


extern int websockets_client_send(struct websocket_struct *websockets_info, u8 *buf, int len, char type);

int send_encoded_data(const unsigned char *bin_data, size_t data_len, FrameStatus status, struct websocket_struct *web_info, char type)
{
    int ret = -1;
    unsigned char base64_buf[BASE64_BUF_LEN] = {0};
    size_t out_len = 0;
    cJSON *root = NULL;
    cJSON *data_obj = NULL;
    char *json_str = NULL;

    /* if(mbedtls_base64_encode(base64_buf, BASE64_BUF_LEN, &out_len,  */
    /* bin_data, data_len) != 0) { */
    /* printf("Base64 encode failed\n"); */
    /* return -1; */
    /* } */

    root = cJSON_CreateObject();
    if (!root) {
        goto cleanup;
    }

    data_obj = cJSON_CreateObject();
    if (!data_obj) {
        goto cleanup;
    }

    if (status == STATUS_FIRST_FRAME) {
        cJSON *common = cJSON_CreateObject();
        cJSON *business = cJSON_CreateObject();

        cJSON_AddStringToObject(common, "app_id", TCFG_IFLYTEK_APP_ID);
        cJSON_AddItemToObject(root, "common", common);

        cJSON_AddStringToObject(business, "language", "zh_cn");
        cJSON_AddStringToObject(business, "domain", "iat");
        cJSON_AddStringToObject(business, "accent", "mandarin");
        cJSON_AddNumberToObject(business, "speex_size", SPEEX_SIZE);
        cJSON_AddItemToObject(root, "business", business);
    }
    cJSON_AddNumberToObject(data_obj, "status", status);
    cJSON_AddStringToObject(data_obj, "format", "audio/L16;rate=16000");
    cJSON_AddStringToObject(data_obj, "encoding", "speex-wb");
    /* cJSON_AddStringToObject(data_obj, "audio", (char*)base64_buf); */
    cJSON_AddStringToObject(data_obj, "audio", (char *)bin_data);
    cJSON_AddItemToObject(root, "data", data_obj);

    json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        goto cleanup;
    }

    ret = websockets_client_send(web_info,
                                 (unsigned char *)json_str,
                                 strlen(json_str),
                                 type);

cleanup:
    if (root) {
        cJSON_Delete(root);
    }
    if (json_str) {
        free(json_str);
    }
    return ret;
}

extern char *my_test_data[40];

FrameStatus frame_status = STATUS_FIRST_FRAME;
int process_string(const char *str, struct websocket_struct *web_info, char type)
{
    static int index = 0;
    int ret = -1;
    int read_bytes;
    if (index < 40) {
        if (index == 39) {

            frame_status = STATUS_LAST_FRAME;
        }
        read_bytes = strlen(my_test_data[index]);
        ret = send_encoded_data((u8 *)my_test_data[index], read_bytes, frame_status, web_info, type);
    }

    index++;
    return ret;
}

int send_file_via_websocket(struct websocket_struct *web_info, char type)
{
    int ret = process_string(NULL, web_info, WCT_TXTDATA);
    if (frame_status == STATUS_FIRST_FRAME) {
        frame_status = STATUS_CONTINUE_FRAME;
    }
    return ret;
}



char *my_test_txt = "5rip5bqmAA==";




void ifly_tts_test(struct websocket_struct *web_info, char type)
{
    static int cnt = 0;
    if (cnt) {
        return;
    }
    cnt++;
    printf(">>>>>>>>zwz  %s %d \n", __func__, __LINE__);
    char *data_str = NULL;
    int out_len = 0;
    cJSON *cjson_test = NULL;
    cJSON *cjson_common = NULL;
    cJSON *cjson_business = NULL;
    cJSON *cjson_data = NULL;

    /* char *buf = malloc(MAX_SPARKDESK_LEN); */
    /* mbedtls_base64_encode(buf, MAX_SPARKDESK_LEN, &out_len, tts_info.param->text_res, strlen(tts_info.param->text_res) + 1); */
//定义最长回答

    cjson_test = cJSON_CreateObject();
    cjson_common = cJSON_CreateObject();
    cjson_business = cJSON_CreateObject();
    cjson_data = cJSON_CreateObject();

    cJSON_AddStringToObject(cjson_common, "app_id", TCFG_IFLYTEK_APP_ID);
    cJSON_AddItemToObject(cjson_test, "common", cjson_common);

    cJSON_AddStringToObject(cjson_business, "aue", "lame");
    cJSON_AddNumberToObject(cjson_business, "sfl", 1);
    cJSON_AddStringToObject(cjson_business, "auf", "audio/L16;rate=16000");
    cJSON_AddStringToObject(cjson_business, "vcn", "xiaoyan");
    cJSON_AddStringToObject(cjson_business, "tte", "UTF8");
    cJSON_AddItemToObject(cjson_test, "business", cjson_business);

    cJSON_AddNumberToObject(cjson_data, "status", 2);
    /* cJSON_AddStringToObject(cjson_data, "text", buf); */
    cJSON_AddStringToObject(cjson_data, "text", my_test_txt);
    cJSON_AddItemToObject(cjson_test, "data", cjson_data);

    data_str = cJSON_Print(cjson_test);
    if (!data_str) {
        goto cleanup;
    }

    int ret = websockets_client_send(web_info,
                                     (unsigned char *)data_str,
                                     strlen(data_str),
                                     type);
cleanup:
    /* if(buf) */
    /* { */
    /* free(buf); */
    /* } */
    if (cjson_test) {

        cJSON_Delete(cjson_test);
    }

    printf("tts content:%s\n", data_str);
}





#include "ifly_sparkdesk.h"
void ifly_sparkdesk_test_demo(struct websocket_struct *web_info, char type)
{
    char *data_str = NULL;
    cJSON *cjson_test = NULL;
    cJSON *cjson_header = NULL;
    cJSON *cjson_parameter = NULL;
    cJSON *cjson_payload = NULL;
    cJSON *cjson_chat = NULL;
    cJSON *cjson_message = NULL;
    cJSON *cjson_text = NULL;
    cJSON *cjson_texts = NULL;

    cjson_test = cJSON_CreateObject();
    cjson_header = cJSON_CreateObject();
    cjson_parameter = cJSON_CreateObject();
    cjson_payload = cJSON_CreateObject();
    cjson_chat = cJSON_CreateObject();
    cjson_message = cJSON_CreateObject();
    cjson_text = cJSON_CreateObject();

    cJSON_AddStringToObject(cjson_header, "app_id", TCFG_IFLYTEK_APP_ID);
    cJSON_AddItemToObject(cjson_test, "header", cjson_header);

    cJSON_AddStringToObject(cjson_chat, "domain", "generalv3");
    cJSON_AddNumberToObject(cjson_chat, "max_tokens", MAX_SPARKDESK_TOKENS);
    cJSON_AddItemToObject(cjson_parameter, "chat", cjson_chat);
    cJSON_AddItemToObject(cjson_test, "parameter", cjson_parameter);

    cjson_texts = cJSON_AddArrayToObject(cjson_message, "text");
    cJSON_AddStringToObject(cjson_text, "role", "user");
    /* cJSON_AddStringToObject(cjson_text, "content", sparkdesk_info.param->content); */

    cJSON_AddStringToObject(cjson_text, "content", "温度");
    cJSON_AddItemToArray(cjson_texts, cjson_text);
    cJSON_AddItemToObject(cjson_payload, "message", cjson_message);
    cJSON_AddItemToObject(cjson_test, "payload", cjson_payload);

    data_str = cJSON_Print(cjson_test);
    if (!data_str) {
        goto cleanup;
    }
    int ret = websockets_client_send(web_info,
                                     (unsigned char *)data_str,
                                     strlen(data_str),
                                     type);

cleanup:
    cJSON_Delete(cjson_test);

}













#endif
