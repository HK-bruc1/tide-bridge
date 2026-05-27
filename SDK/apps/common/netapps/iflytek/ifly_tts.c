#include "ifly_tts.h"
#include "ifly_auth.h"
#include "ifly_socket.h"
#include "websocket_api.h"

#if TCFG_IFLYTEK_ENABLE
static ifly_tts_struct *p_ifly_net = NULL;

static void ifly_tts_recv_cb(u8 *j_str, u32 len, u8 type)
{
    printf("func  %s  line %d str %s \n", __func__, __LINE__, j_str);

    extern void ifly_tts_del();
    ifly_tts_del();
#if 0
    tts_info.tts_to_cnt = 0;   //收到数据重置定时器

    if (tts_info.force_stop) {
        return;
    }
    if (tts_info.status >= IFLY_TTS_STATUS_RECV_END) {
        return;
    }

    cJSON *cjson_root = cJSON_Parse(j_str);
    char *audio = NULL;
    char *res_tts = NULL;
    if (cjson_root == NULL) {
        log_error("cjson error...\r\n");
        if (tts_info.status <= IFLY_TTS_STATUS_RECV_END) {
            tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        }
        return;
    }

    cJSON *cjson_data = cJSON_GetObjectItem(cjson_root, "data");
    cJSON *cjson_status = cJSON_GetObjectItem(cjson_data, "status");
    cJSON *cjson_audio = cJSON_GetObjectItem(cjson_data, "audio");

    audio = cJSON_Print(cjson_audio);
    if (!audio) {
        log_error("audio is null!!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        return;
    }

    str_remove_quote(audio, strlen(audio));

    res_tts = malloc(IFLY_TTS_PKG_MAX);
    if (!res_tts) {
        log_error("malloc fail!!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        return;
    }
    int olen = 0;
    int ret = mbedtls_base64_decode(res_tts, IFLY_TTS_PKG_MAX, &olen, audio, strlen(audio));
    if (ret || (olen > IFLY_TTS_PKG_MAX)) {
        log_error("mbedtls_base64_decode fail!!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_ERROR;
        return;
    }


#if TTS_AUDIO_SAVE_TEST
    if (save_file) {
        int wlen = fwrite(save_file, res_tts, olen);
        if (wlen != olen) {
            log_error("save file err: %d, %d\n", wlen, olen);
        }
    }
#endif


    //若cbuf满了，需要先while住，等待播放
    int to = 1500;
    while (!cbuf_write(&tts_info.dec_cbuf, res_tts, olen)) {
        if (tts_info.force_stop) {
            break;
        }
        os_time_dly(1);
        if (to < 10) {
            log_info(">>>>>>>>>>>>>>>>>>>>>>>cbuf full");
            break;
        }
        to -= 10;
    }

    if (tts_info.status < IFLY_TTS_STATUS_RECV) {
        tts_info.status = IFLY_TTS_STATUS_RECV;
        log_info("tts start play!\n");

        ifly_tts_dec_func(ifly_net_tts_start, 0);
        tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_START, tts_info.param);
    }
    if (cjson_status->valueint == RECV_LAST_FRAME) {
        log_info("tts last frame!\n");
        tts_info.status = IFLY_TTS_STATUS_RECV_END;
        tts_info.recv_finish = 1;

        ifly_tts_dec_func(ifly_net_tts_stop, 3000);
        tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_STOP, tts_info.param);
    }

    cJSON_free(audio);
    cJSON_Delete(cjson_root);
    free(res_tts);

    ifly_net_tts_dec_resume(NULL);
#endif
}

static bool ifly_tts_get_send(char **buf, u32 *len)
{
#if 0
    if (tts_info.force_stop) {
        log_info("tts task kill!\n");
        return false;
    }
    if (tts_info.status >= IFLY_TTS_STATUS_PLAY_END) {
        log_info("tts task kill!\n");
        return false;
    }
    if (tts_info.status < IFLY_TTS_STATUS_SEND) {
        tts_info.status = IFLY_TTS_STATUS_SEND;
        char *input_src_json = ifly_tts_format_text_data();
        r_printf("%s \n", input_src_json);
        if (input_src_json == NULL) {
            log_error("get json err \n");
            return false;
        }
        *buf = input_src_json;
        *len = strlen(input_src_json);
        return true;
    }
    os_time_dly(2);
#endif
    return true;
}

static int ifly_tts_event_cb(ifly_socket_event_enum evt, void *param)
{
    switch (evt) {
    case IFLY_SOCKET_EVT_SEND_OK:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_SOCKET_EVT_SEND_ERROR:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_SOCKET_EVT_INIT_OK:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_SOCKET_EVT_INIT_ERROR:
    case IFLY_SOCKET_EVT_HANSHACK_ERROR:
    case IFLY_SOCKET_EVT_ACCIDENT_END:
    case IFLY_SOCKET_EVT_END:
    case IFLY_SOCKET_EVT_FORCE_END:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_SOCKET_EVT_EXIT:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    default:
        break;
    }
    return 0;
}


static tts_info_t tts_info;
static ifly_socket_param tts_socket;
bool ifly_tts_start(ifly_tts_param *param)
{
    memset(&tts_info, 0, sizeof(tts_info_t));
    memset(&tts_socket, 0, sizeof(ifly_socket_param));

    tts_info.param = param;

    tts_info.dec_out_buf = malloc(sizeof(char) * IFLY_TTS_CBUF_LEN);
    cbuf_init(&tts_info.dec_cbuf, tts_info.dec_out_buf, IFLY_TTS_CBUF_LEN);

    tts_socket.auth = ifly_authentication("ws://tts-api.xfyun.cn/v2/tts",
                                          "tts-api.xfyun.cn",
                                          "GET /v2/tts HTTP/1.1");
    if (!tts_socket.auth) {
        free(tts_info.dec_out_buf);
        tts_info.dec_out_buf = NULL;;
        tts_info.param->event_cb(IFLY_TTS_EVT_PLAY_FAIL_STOP, tts_info.param);
        return false;
    }
    tts_socket.task_name = "ifly_tts";
    tts_socket.socket_mode = WEBSOCKET_MODE;
    tts_socket.recv_cb = ifly_tts_recv_cb;
    tts_socket.get_send = ifly_tts_get_send;
    tts_socket.event_cb = ifly_tts_event_cb;

    tts_info.status = IFLY_TTS_STATUS_START;

    //创建链接
    bool ret = ifly_websocket_client_create(&tts_socket);
    if (ret == false) {
        tts_info.status = IFLY_TTS_STATUS_NULL;
        free(tts_socket.auth);
        tts_socket.auth = NULL;
        free(tts_info.dec_out_buf);
        tts_info.dec_out_buf = NULL;;
    }

#if TTS_AUDIO_SAVE_TEST
    if (save_file) {
        fclose(save_file);
        save_file = NULL;
    }
    save_file = fopen("storage/sd0/C/sf.mp3", "w+");
    g_printf(">>>>>>>zwz  info  %s %d \n", __func__, __LINE__);
    if (!save_file) {
        log_error("fopen err \n\n");
    }
#endif
    return ret;
}
static int my_ifly_tts_event_cb(ifly_tts_event_enum evt, void *param)
{
    switch (evt) {
    case IFLY_TTS_EVT_PLAY_START:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_TTS_EVT_PLAY_STOP:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_TTS_EVT_PLAY_FAIL_STOP:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    case IFLY_TTS_EVT_EXIT:
        printf(">>>>>zwz info %s  %d \n", __func__, __LINE__);
        break;
    default:
        break;
    }
    return 0;
}

void ifly_tts_demo(void)
{
    static int zwz = 0;
    if (zwz) {
        return;
    }
    zwz++;
    if (p_ifly_net == NULL) {
        p_ifly_net = zalloc(sizeof(ifly_tts_struct));
        ASSERT(p_ifly_net);
    }
    strcpy(p_ifly_net->ai_text, "温度");
    p_ifly_net->tts_param.event_cb = my_ifly_tts_event_cb;
    p_ifly_net->tts_param.text_res = p_ifly_net->ai_text;

    ifly_tts_start(&p_ifly_net->tts_param);  //创建vad任务
}


#endif
