#include "ifly_vad.h"
#include "ifly_socket.h"
#include "ifly_auth.h"
#include "websocket_define.h"
#include "ifly_common.h"

#if TCFG_IFLYTEK_ENABLE
static vad_info_t vad_info;
static ifly_socket_param vad_socket;

static void ifly_vad_recv_cb(u8 *j_str, u32 len, u8 type)
{
    printf("recv:%s\n", j_str);
}


//语音听写数据模块
char *ifly_vad_format_audio_data(void)
{
#if 0
    char *data_str = NULL;
    cJSON *cjson_test = NULL;
    cJSON *cjson_common = NULL;
    cJSON *cjson_business = NULL;
    cJSON *cjson_data = NULL;

    size_t out_len = 0;
    unsigned char *buf = malloc(BASE63_AUDIO_LEN);
    unsigned char *audio_data = malloc(AUDIO_LEN);
    ASSERT(buf);
    ASSERT(audio_data);

    int rlen = cbuf_read(&vad_info.pcm_cbuf, audio_data, AUDIO_LEN);
    if (rlen != AUDIO_LEN) {
        free(buf);
        free(audio_data);
        return NULL;
    }

    if (vad_info.frame_status == STATUS_FIRST_FRAME) {

        mbedtls_base64_encode(buf, BASE63_AUDIO_LEN, &out_len, audio_data, AUDIO_LEN);

        //编码第一帧
        cjson_test = cJSON_CreateObject();
        cjson_common = cJSON_CreateObject();
        cjson_business = cJSON_CreateObject();
        cjson_data = cJSON_CreateObject();

        cJSON_AddStringToObject(cjson_common, "app_id", TCFG_IFLYTEK_APP_ID);
        cJSON_AddItemToObject(cjson_test, "common", cjson_common);

        cJSON_AddStringToObject(cjson_business, "language", "zh_cn");
        cJSON_AddStringToObject(cjson_business, "domain", "iat");
        cJSON_AddStringToObject(cjson_business, "accent", "mandarin");
        cJSON_AddNumberToObject(cjson_business, "speex_size", SPEEX_SIZE);
        cJSON_AddItemToObject(cjson_test, "business", cjson_business);

        cJSON_AddNumberToObject(cjson_data, "status", 0);
        cJSON_AddStringToObject(cjson_data, "format", "audio/L16;rate=16000");
        cJSON_AddStringToObject(cjson_data, "encoding", "speex-wb");
        cJSON_AddStringToObject(cjson_data, "audio", (char *)buf);
        cJSON_AddItemToObject(cjson_test, "data", cjson_data);

        data_str = cJSON_Print(cjson_test);

        cJSON_Delete(cjson_test);

        vad_info.frame_status = STATUS_CONTINUE_FRAME;

    } else if (vad_info.frame_status == STATUS_CONTINUE_FRAME) {

        mbedtls_base64_encode(buf, BASE63_AUDIO_LEN, &out_len, audio_data, AUDIO_LEN);

        //编码
        cjson_test = cJSON_CreateObject();
        cjson_data = cJSON_CreateObject();

        cJSON_AddNumberToObject(cjson_data, "status", 1);
        cJSON_AddStringToObject(cjson_data, "format", "audio/L16;rate=16000");
        cJSON_AddStringToObject(cjson_data, "encoding", "speex-wb");
        cJSON_AddStringToObject(cjson_data, "audio", (char *)buf);
        cJSON_AddItemToObject(cjson_test, "data", cjson_data);

        data_str = cJSON_Print(cjson_test);

        cJSON_Delete(cjson_test);

    } else {
        //编码最后一帧
        mbedtls_base64_encode(buf, BASE63_AUDIO_LEN, &out_len, audio_data, AUDIO_LEN);

        cjson_test = cJSON_CreateObject();
        cjson_data = cJSON_CreateObject();

        cJSON_AddNumberToObject(cjson_data, "status", 2);
        cJSON_AddStringToObject(cjson_data, "format", "audio/L16;rate=16000");
        cJSON_AddStringToObject(cjson_data, "encoding", "speex-wb");
        cJSON_AddStringToObject(cjson_data, "audio", (char *)buf);
        cJSON_AddItemToObject(cjson_test, "data", cjson_data);

        data_str = cJSON_Print(cjson_test);

        cJSON_Delete(cjson_test);

        if (vad_info.status < IFLY_VAD_STATUS_SEND_END) {
            vad_info.status = IFLY_VAD_STATUS_SEND_END;
            vad_audio_stop(1);
            cbuf_clear(&vad_info.pcm_cbuf);
        }
    }

    free(buf);
    free(audio_data);

    return data_str;
#endif
    return NULL;
}

static bool ifly_vad_get_send(char **buf, u32 *len)
{
    return true;
}

static int ifly_vad_event_cb(ifly_socket_event_enum evt, void *param)
{
    switch (evt) {
    case IFLY_SOCKET_EVT_SEND_OK:
        break;
    case IFLY_SOCKET_EVT_SEND_ERROR:
        break;
    case IFLY_SOCKET_EVT_INIT_OK:
        break;
    case IFLY_SOCKET_EVT_INIT_ERROR:
    case IFLY_SOCKET_EVT_HANSHACK_ERROR:
    case IFLY_SOCKET_EVT_ACCIDENT_END:
    case IFLY_SOCKET_EVT_END:
    case IFLY_SOCKET_EVT_FORCE_END:
        break;
    case IFLY_SOCKET_EVT_EXIT:
        break;
    default:
        break;
    }
    return 0;
}

bool ifly_vad_start(ifly_vad_param *param)
{
    memset(&vad_info, 0, sizeof(vad_info_t));
    memset(&vad_socket, 0, sizeof(ifly_socket_param));
    vad_info.pcm_out_buf = malloc(PCM_OUT_BUF_LEN);
    ASSERT(vad_info.pcm_out_buf);
    cbuf_init(&vad_info.pcm_cbuf, vad_info.pcm_out_buf, PCM_OUT_BUF_LEN);

    vad_info.param = param;
    vad_socket.auth = ifly_authentication("ws://iat-api.xfyun.cn/v2/iat",
                                          "iat-api.xfyun.cn",
                                          "GET /v2/iat HTTP/1.1");
    if (!vad_socket.auth) {
        free(vad_info.pcm_out_buf);
        vad_info.pcm_out_buf = NULL;
        vad_info.param->event_cb(IFLY_VAD_EVT_NETWORK_FAIL, vad_info.param);
        return false;
    }
    vad_socket.task_name = IFLY_VAD_TASK_NAME;
    vad_socket.socket_mode = WEBSOCKET_MODE;
    vad_socket.recv_cb = ifly_vad_recv_cb;
    vad_socket.get_send = ifly_vad_get_send;
    vad_socket.event_cb = ifly_vad_event_cb;

    vad_info.status = IFLY_VAD_STATUS_START;

    //创建链接
    bool ret = ifly_websocket_client_create(&vad_socket);
    if (ret == false) {
        vad_info.status = IFLY_VAD_STATUS_NULL;
        free(vad_socket.auth);
        vad_socket.auth = NULL;
        free(vad_info.pcm_out_buf);
        vad_info.pcm_out_buf = NULL;
    }
    return ret;
}


void ifly_vad_stop(u8 force_stop, u32 to_ms)
{
    printf("vad stop!\n");

    extern void ifly_task_del();
    ifly_task_del();
    /*
        ifly_vad_audio_stop();
        vad_info.force_stop = force_stop;
        while (vad_socket.auth) { // 结束时auth会自动释放
            os_time_dly(1);
            if (to_ms <= 10) {
                break;
            }
            to_ms -= 10;
        }
        if (to_ms < 1000) {
            to_ms = 1000;
        }
        vad_info.force_stop = 1;
        ifly_websocket_client_release(&vad_socket, to_ms);
    */
}

void ifly_vad_audio_stop(void)
{
    vad_info.frame_status = STATUS_LAST_FRAME; // 停止语音发送
}

bool ifly_vad_is_work()
{
    if ((vad_info.status != IFLY_VAD_STATUS_NULL) && (vad_info.status != IFLY_VAD_STATUS_EXIT)) {
        return true;
    }
    return false;
}

#endif
