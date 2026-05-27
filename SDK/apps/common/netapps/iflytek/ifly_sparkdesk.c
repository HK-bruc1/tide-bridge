#include "ifly_sparkdesk.h"
#include "ifly_socket.h"
#include "ifly_auth.h"
#include "websocket_define.h"
#include "cJSON.h"

#if TCFG_IFLYTEK_ENABLE
static sparkdesk_info_t sparkdesk_info;
static ifly_socket_param sparkdesk_socket;

//json解析函数_ai
static void ifly_sparkdesk_recv_cb(u8 *j_str, u32 len, u8 type)
{
    printf(">>>>>>> %s %d j_str %s \n", __func__, __LINE__, j_str);
#if 0
    if (sparkdesk_info.force_stop) {
        return;
    }
    if (sparkdesk_info.status >= IFLY_AI_STATUS_RECV_END) {
        return;
    }
    cJSON *cjson_root = cJSON_Parse(j_str);
    if (cjson_root == NULL) {
        log_error("cjson error...\r\n");
        if (sparkdesk_info.status <= IFLY_AI_STATUS_RECV_END) {
            sparkdesk_info.status = IFLY_AI_STATUS_RECV_ERROR;
        }
        return;
    }
    sparkdesk_info.status = IFLY_AI_STATUS_RECV;

    cJSON *cjson_header = cJSON_GetObjectItem(cjson_root, "header");
    cJSON *cjson_status = cJSON_GetObjectItem(cjson_header, "status");
    cJSON *cjson_payload = cJSON_GetObjectItem(cjson_root, "payload");
    cJSON *cjson_choices = cJSON_GetObjectItem(cjson_payload, "choices");
    cJSON *cjson_text = cJSON_GetObjectItem(cjson_choices, "text");

    int arr_size = cJSON_GetArraySize(cjson_text);
    cJSON *arr_item = cjson_text->child;
    u32 ai_res_len = strlen(sparkdesk_info.param->ai_res);
    for (int i = 0; i < arr_size; i++) {
        cJSON *cjson_content = cJSON_GetObjectItem(arr_item, "content");
        char *cjson_str = cJSON_Print(cjson_content);
        u32 json_len = strlen(cjson_str);
        if ((ai_res_len + json_len + 1) > sparkdesk_info.param->ai_res_len) {
            log_error("len error\n");
        } else {
            strcpy(&sparkdesk_info.param->ai_res[ai_res_len], cjson_str);
            ai_res_len += json_len;
        }
        arr_item = arr_item->next;
        cJSON_free(cjson_str);
    }

    int res_len = strlen(sparkdesk_info.param->ai_res);

    str_remove_quote(sparkdesk_info.param->ai_res, res_len);

    log_info("final res:%s\n", sparkdesk_info.param->ai_res);

    if (cjson_status->valueint == RECV_LAST_FRAME) {
        sparkdesk_info.status = IFLY_AI_STATUS_RECV_END;
        sparkdesk_info.recv_finish = 1;
        sparkdesk_info.param->event_cb(IFLY_AI_EVT_RECV_OK, sparkdesk_info.param);
    }

    cJSON_Delete(cjson_root);
#endif
}


//ai数据模块
static char *ifly_sparkdesk_format_audio_data(void)
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

    cJSON_AddStringToObject(cjson_header, "app_id", APP_ID);
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

    cJSON_Delete(cjson_test);

    return data_str;
}

static bool ifly_sparkdesk_get_send(char **buf, u32 *len)
{
#if 0
    if (sparkdesk_info.force_stop) {
        log_info("ai task kill!\n");
        return false;
    }
    if (sparkdesk_info.status >= IFLY_AI_STATUS_RECV_END) {
        log_info("ai task kill!\n");
        return false;
    }
    if (sparkdesk_info.status < IFLY_AI_STATUS_SEND) {
        sparkdesk_info.status = IFLY_AI_STATUS_SEND;
        char *input_src_json = ifly_sparkdesk_format_audio_data();
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

static int ifly_sparkdesk_event_cb(ifly_socket_event_enum evt, void *param)
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

bool ifly_sparkdesk_start(ifly_ai_param *param)
{
    memset(&sparkdesk_info, 0, sizeof(sparkdesk_info_t));
    memset(&sparkdesk_socket, 0, sizeof(ifly_socket_param));

    sparkdesk_info.param = param;
    sparkdesk_socket.auth = ifly_authentication("ws://spark-api.xf-yun.com/v3.1/chat",
                            "spark-api.xf-yun.com",
                            "GET /v3.1/chat HTTP/1.1");
    if (!sparkdesk_socket.auth) {
        sparkdesk_info.param->event_cb(IFLY_AI_EVT_NETWORK_FAIL, sparkdesk_info.param);
        return false;
    }

    sparkdesk_socket.task_name = "ifly_ai";
    sparkdesk_socket.socket_mode = WEBSOCKET_MODE;
    sparkdesk_socket.recv_cb = ifly_sparkdesk_recv_cb;
    sparkdesk_socket.get_send = ifly_sparkdesk_get_send;
    sparkdesk_socket.event_cb = ifly_sparkdesk_event_cb;

    sparkdesk_info.status = IFLY_AI_STATUS_START;

    //创建链接
    bool ret = ifly_websocket_client_create(&sparkdesk_socket);
    if (ret == false) {
        sparkdesk_info.status = IFLY_AI_STATUS_NULL;
        free(sparkdesk_socket.auth);
        sparkdesk_socket.auth = NULL;
    }

    return ret;
}

void ifly_sparkdesk_stop(u8 force_stop, u32 to_ms)
{
    printf("ai stop!\n");
#if 0
    sparkdesk_info.force_stop = force_stop;
    while (sparkdesk_socket.auth) { // 结束时auth会自动释放
        os_time_dly(1);
        if (to_ms <= 10) {
            break;
        }
        to_ms -= 10;
    }
    if (to_ms < 1000) {
        to_ms = 1000;
    }
    sparkdesk_info.force_stop = 1;
    ifly_websocket_client_release(&sparkdesk_socket, to_ms);
#endif
}

bool ifly_sparkdesk_is_work()
{
#if 0
    if ((sparkdesk_info.status != IFLY_AI_STATUS_NULL) && (sparkdesk_info.status != IFLY_AI_STATUS_EXIT)) {
        return true;
    }
#endif
    return false;
}

static int my_ifly_ai_event_cb(ifly_ai_event_enum evt, void *param)
{
    switch (evt) {
    case IFLY_AI_EVT_NETWORK_FAIL:
        break;
    case IFLY_AI_EVT_RECV_OK:
        break;
    case IFLY_AI_EVT_EXIT:
        break;
    default:
        break;
    }
    return 0;
}

static ifly_ai_struct *p_ifly_net = NULL;
void ifly_sparkdesk_demo()
{
    static int zwz = 0;
    if (zwz) {
        return;
    }
    zwz++;
    if (p_ifly_net == NULL) {
        p_ifly_net = zalloc(sizeof(ifly_ai_struct));
        ASSERT(p_ifly_net);
    }
    p_ifly_net->ai_param.ai_res = p_ifly_net->ai_text;
    p_ifly_net->ai_param.ai_res_len = MAX_SPARKDESK_LEN;
    p_ifly_net->ai_param.event_cb = my_ifly_ai_event_cb;
    p_ifly_net->ai_param.content = "温度";
    p_ifly_net->ai_param.ai_res[0] = 0;
    ifly_sparkdesk_start(&p_ifly_net->ai_param);
}
#endif
