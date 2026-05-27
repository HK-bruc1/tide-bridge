#include "ifly_auth.h"
#include "mbedtls_3_4_0/mbedtls/base64.h"
#include "mbedtls_3_4_0/mbedtls/md.h"
#include "ifly_common.h"

#if TCFG_IFLYTEK_ENABLE

void *__attribute__((weak))_calloc_r(struct _reent *r, size_t a, size_t b)
{
    return calloc(a, b);
}

static char dec2hex(short int c)
{
    if (0 <= c && c <= 9) {
        return c + '0';
    } else if (10 <= c && c <= 15) {
        return c + 'A' - 10;
    } else {
        return -1;
    }
}

static void urlencode(char url[])
{
    int i = 0;
    int len = strlen(url);
    int res_len = 0;
    char res[100];
    for (i = 0; i < len; ++i) {
        char c = url[i];
        if (('0' <= c && c <= '9') ||
            ('a' <= c && c <= 'z') ||
            ('A' <= c && c <= 'Z') ||
            c == '/' || c == '.') {
            res[res_len++] = c;
        } else {
            int j = (short int)c;
            if (j < 0) {
                j += 256;
            }
            int i1, i0;
            i1 = j / 16;
            i0 = j - i1 * 16;
            res[res_len++] = '%';
            res[res_len++] = dec2hex(i1);
            res[res_len++] = dec2hex(i0);
        }
    }
    res[res_len] = '\0';
    strcpy(url, res);
}

static void ifly_get_sys_time(struct sys_time *time)//获取时间
{
}

char *ifly_authentication(char *host_name, char *host, char *path)
{
    auth_info_t *auth = zalloc(sizeof(auth_info_t));
    char *url_xf = zalloc(400);
    time_t t;
    time_t curtime = time(&t);
    printf(">>>>>>>>>>>info %s  %d curtime %ld \n", __func__, __LINE__, curtime);
    struct tm *gmt_tm = gmtime(&curtime);
    printf("my_time %d:%d:%d %d:%d:%d", gmt_tm->tm_year, gmt_tm->tm_mon, gmt_tm->tm_mday, gmt_tm->tm_hour, gmt_tm->tm_min, gmt_tm->tm_sec);
    strftime_2(auth->date_str, 40, "", gmt_tm);
    printf("%s\n", auth->date_str);
    sprintf(auth->sig_ori, "%s%s\ndate: %s\n%s", "host: ", host, auth->date_str, path);
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info;
    mbedtls_md_init(&ctx);
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (unsigned char *)TCFG_IFLYTEK_APP_SECRET, strlen(TCFG_IFLYTEK_APP_SECRET));
    mbedtls_md_hmac_update(&ctx, (unsigned char *)auth->sig_ori, strlen(auth->sig_ori));
    mbedtls_md_hmac_finish(&ctx, (unsigned char *)auth->sig_hmac);
    mbedtls_md_free(&ctx);
    int res;
    mbedtls_base64_encode((unsigned char *)auth->sig_hmac_base64, 50, (size_t *)&res, (unsigned char *)auth->sig_hmac, strlen(auth->sig_hmac));
    //拼接authorization_ori字符串
    strcat(auth->author_ori, "api_key=\"");
    strcat(auth->author_ori, TCFG_IFLYTEK_APP_KEY);
    strcat(auth->author_ori, "\",");
    strcat(auth->author_ori, "algorithm=\"hmac-sha256\",");
    strcat(auth->author_ori, "headers=\"host date request-line\",");
    strcat(auth->author_ori, "signature=\"");
    strcat(auth->author_ori, auth->sig_hmac_base64);
    strcat(auth->author_ori, "\"");
    //对autho_ori进行base64编码
    int author_res;
    mbedtls_base64_encode((unsigned char *)auth->author, 300, (size_t *)&author_res, (unsigned char *)auth->author_ori, strlen(auth->author_ori));
    //拼接url
    strcat(url_xf, host_name);
    strcat(url_xf, "?");
    urlencode(auth->date_str);
    strcat(url_xf, "authorization=");
    strcat(url_xf, auth->author);
    strcat(url_xf, "&date=");
    strcat(url_xf, auth->date_str);
    strcat(url_xf, "&host=");
    strcat(url_xf, host);
    printf("url_xf:%s\n", url_xf);

    free(auth);
    return url_xf;
}
#endif
